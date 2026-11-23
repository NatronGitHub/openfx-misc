/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/NatronGitHub/openfx-misc>,
 * (C) 2018-2021 The Natron Developers
 * (C) 2013-2018 INRIA
 *
 * openfx-misc is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * openfx-misc is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with openfx-misc.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

/*
 * OFX Retime plugin.
 * Change the timing of the input clip.
 */

/*
   TODO: this plugin has to be improved a lot
   - propose a "timewarp" curve (as ParametricParam)
   - shutter time for the Box filter (it is one output frame)
   - handle fielded input correctly

   - retiming based on optical flow computation will be done separately
 */

#include <cmath> // for floor
#include <cfloat> // DBL_MAX
#include <cassert>
#include <climits> // INT_MAX
#include <algorithm> // std::sort
#include <optional>
#include <vector>

#include "ofxsImageEffect.h"
#include "ofxsThreadSuite.h"
#include "ofxsMultiThread.h"

#include "ofxsProcessing.H"
#include "ofxsImageBlender.H"
#include "ofxsCopier.h"
#include "ofxsMacros.h"
#include "ofxsCoords.h"
#include "../FrameRange/FrameRangeBeforeAfter.h"

using namespace OFX;
using namespace OFX::FrameRangeBeforeAfter;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "RetimeOFX"
#define kPluginGrouping "Time"
#define kPluginDescription "Change the timing of the input clip.\n" \
    "In the general context (e.g. in Natron), the output frame range starts with the input frame range, and lasts as long as it takes to play the input frame range at the given Speed (e.g. twice as long at speed 0.5), forwards, or backwards with Reverse input. It is the input frame range if the speed is 0 or negative, or so small that the output would last more than 1000 times the input or 100000 frames. A speed without keys (e.g. an expression) is taken as constant, with its value at the first input frame. On hosts without parametric parameters, the Duration parameter sets it instead.\n" \
    "The Before and After parameters set what is read when the source frame falls outside the frame range of the input clip: the input frame itself (Original), the first or last frame (Hold), transparent black (Black), or frames repeated from the input range (Loop, Bounce).\n" \
    "Before and After refer to the source frame range, not to the output: when the input is played backwards (Reverse input, or a negative speed or warp slope), Before sets what is shown at the end of the output.\n" \
    "See also: https://web.archive.org/web/20220627032808/http://www.opticalenquiry.com/nuke/index.php?title=Retime"

#define kPluginIdentifier "net.sf.openfx.Retime"
// History:
// version 1.0: initial version
// version 1.1: a NULL Source image (disconnected input; render is called although isIdentity returned true,
//              e.g. by hostDemo) renders black instead of crashing (SupportExt copyPixels);
//              (#309, in the current minor version, not released yet) in the general context, the time
//              domain (output frame range) follows the speed: it ends where the integrated speed covers the
//              input frame range (it was the input frame range at any speed), except with the Duration
//              parameter (hosts without parametric parameters), which still sets it. In old projects, this
//              also changes what the nodes downstream see as their input frame range (e.g. Write,
//              FrameRange, FrameHold, the Before/After of another Retime). The Box filter
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 1 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamReverseInput "reverseInput"
#define kParamReverseInputLabel "Reverse input"
#define kParamReverseInputHint "Reverse the order of the input frames so that last one is first"

#define kParamSpeed "speed"
#define kParamSpeedLabel "Speed"
#define kParamSpeedHint "How much to change the speed of the input clip. To determine which input frame is taken at a given time, the speed is integrated from the beginning of the source frame range to the given time, so that speed can be animated to locally accelerate (speed > 1), decelerate (speed < 1) or reverse (speed < 0) the source clip. Note that this is is not the same as the speed parameter of the Nuke Retime node, which just multiplies the speed value at the current time by the time to obtain the source frame number."

#define kParamDuration "duration"
#define kParamDurationLabel "Duration"
#define kParamDurationHint "How long the output clip should be, as a proportion of the input clip's length. Only on hosts without parametric parameters (no Warp curve), where it sets the output frame range: the speed is not used for it."

#define kParamFilter "filter"
#define kParamFilterLabel "Filter"
#define kParamFilterHint "How input images are combined to compute the output image."

#define kParamFilterOptionNone "None", "Do not interpolate, ask for images with fractional time to the input effect. Useful if the input effect can interpolate itself.", "none"
#define kParamFilterOptionNearest "Nearest", "Pick input image with nearest integer time. Half frames are rounded up (e.g. 1.5 gives frame 2).", "nearest"
#define kParamFilterOptionLinear "Linear", "Blend the two nearest images with linear interpolation.", "linear"
#define kParamFilterOptionBox "Box", "Average the input frames covered by the output frame (from half a frame before it to half a frame after it, mapped to the input time), each weighted by the part of it that is covered (input frame n covers the times from n-0.5 to n+0.5). At speed 1, this is the input frame; faster speeds blend several frames (motion blur), at most " STRINGISE(kBoxMaxFrames) " frames plus a partial frame at each end. At the ends of the input frame range, the frames outside it are read according to Before/After, so the first and last frames fade (to black, or to what the input gives there) unless Before/After is Hold. In the Retimer context, where the host only gives the source time of the current frame, Box reads the nearest frame.", "box"

enum FilterEnum
{
    eFilterNone,
    eFilterNearest,
    eFilterLinear,
    eFilterBox,
};

// the maximum length, in frames (or fields), of the source interval averaged by the Box filter: beyond
// that, the part around its middle is used. It covers up to kBoxMaxFrames + 1 samples, the first and last
// ones partially.
#define kBoxMaxFrames 64

#define kParamFilterDefault eFilterLinear

#define kParamBeforeHint "What to read when the source frame (computed from the retiming parameters) is before the first frame of the input clip. This refers to source frames: when the input is played backwards, it applies at the end of the output."
#define kParamAfterHint "What to read when the source frame (computed from the retiming parameters) is after the last frame of the input clip. This refers to source frames: when the input is played backwards, it applies at the start of the output."

#define kPageTimeWarp "timeWarp"
#define kPageTimeWarpLabel "Time Warp"

#define kParamWarp "warp"
#define kParamWarpLabel "Warp"
#define kParamWarpHint "Curve that maps input range (after applying speed) to the output range. A low positive slope slows down the input clip, and a negative slope plays it backwards."


// A frame (or field) averaged by the Box filter.
struct BoxSample
{
    double time; // source time
    double weight;
    BeforeAfterEnum mode; // the Before/After behaviour that applies (Black: transparent black)

    bool black() const { return mode == eBeforeAfterBlack; }
};

// The source frames used at a given output time, after applying Before/After.
struct SourceFrames
{
    double from, to; // source times of the frames to read
    bool fromBlack, toBlack; // the frame is transparent black (Before/After = Black)
    bool original; // no Before/After behaviour applies: keeps old projects rendering as before these params existed
    bool blended; // blend "from" and "to" (Linear filter), else only "from" is used
    double blend; // weight of "to"
    bool boxed; // average the frames of box (Box filter, at least two frames), instead of from and to
    std::vector<BoxSample> box; // their weights sum to 1
};

// beyond this, frame numbers could overflow an int in frameLoop/frameBounce
#define kMaxBeforeAfterFrame (INT_MAX / 4)

// Apply Before/After to source time *t. The whole frame (rounded down) is mapped.
// Hold reads the whole first/last frame, so that a held field does not alternate and flicker.
// Loop and Bounce keep the fractional part (second field, or sub-frame time with the None filter),
// mirrored where Bounce plays backwards, so that the second field of the last frame is still in range.
static BeforeAfterEnum
mapSourceTime(int first,
              int last,
              BeforeAfterEnum before,
              BeforeAfterEnum after,
              double *t)
{
    const double frame = std::floor(*t);

    // a range check (the int conversions in getBeforeAfterTime() must not overflow). The source time is
    // computed from host values (the speed integral, the warp curve, the retimer source time) without
    // creating NaN; the plugin does not rely on this test to detect a NaN from the host.
    if (std::fabs(frame) >= kMaxBeforeAfterFrame) {
        return eBeforeAfterOriginal;
    }
    double mapped;
    BeforeAfterEnum mode = getBeforeAfterTime(first, last, before, after, frame, &mapped);
    const double frac = *t - frame;
    if (mode == eBeforeAfterHold) {
        *t = mapped;
    } else if (mode == eBeforeAfterLoop) {
        *t = mapped + frac;
    } else if (mode == eBeforeAfterBounce) {
        double next;
        getBeforeAfterTime(first, last, before, after, frame + 1, &next);
        // in a reversed Bounce segment, the next frame is the previous source frame
        *t = (next == mapped - 1) ? (mapped - frac) : (mapped + frac);
    }

    return mode;
}

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class RetimePlugin
    : public ImageEffect
{
private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;            /**< @brief Mandated output clips */
    Clip *_srcClip;            /**< @brief Mandated input clips */
    BooleanParam  *_reverse_input;
    DoubleParam  *_sourceTime; /**< @brief mandated parameter, only used in the retimer context. */
    DoubleParam  *_speed;      /**< @brief only used in the filter or general context. */
    ParametricParam  *_warp;      /**< @brief only used in the filter or general context. */
    DoubleParam  *_duration;   /**< @brief how long the output should be as a proportion of input. General context only. */
    ChoiceParam  *_filter;   /**< @brief how images are interpolated (or not). */
    ChoiceParam  *_before;
    ChoiceParam  *_after;

public:
    /** @brief ctor */
    RetimePlugin(OfxImageEffectHandle handle,
                 bool supportsParametricParameter)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _reverse_input(NULL)
        , _sourceTime(NULL)
        , _speed(NULL)
        , _warp(NULL)
        , _duration(NULL)
        , _filter(NULL)
        , _before(NULL)
        , _after(NULL)
    {

        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);

        // What parameters we instantiate depend on the context
        if (getContext() == eContextRetimer) {
            // fetch the mandated parameter which the host uses to pass us the frame to retime to
            _sourceTime = fetchDoubleParam(kOfxImageEffectRetimerParamName);
            assert(_sourceTime);
        } else { // context == eContextFilter || context == eContextGeneral
            // filter context means we are in charge of how to retime, and our example is using a speed curve to do that
            _reverse_input = fetchBooleanParam(kParamReverseInput);
            _speed = fetchDoubleParam(kParamSpeed);
            assert(_speed);
            if (supportsParametricParameter) {
                _warp = fetchParametricParam(kParamWarp);
                assert(_warp);
            } else if (getContext() == eContextGeneral) {
                // fetch duration param for general context
                _duration = fetchDoubleParam(kParamDuration);
                assert(_duration);
            }
        }
        _filter = fetchChoiceParam(kParamFilter);
        _before = fetchChoiceParam(kParamBefore);
        _after = fetchChoiceParam(kParamAfter);
        assert(_filter && _before && _after);
    }

    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    template <int nComponents>
    void renderInternal(const RenderArguments &args, const SourceFrames &frames, BitDepthEnum dstBitDepth);

    template <class PIX, int nComponents, int maxValue>
    void renderBox(const RenderArguments &args, const SourceFrames &frames);

    /** Override the get frames needed action */
    virtual void getFramesNeeded(const FramesNeededArguments &args, FramesNeededSetter &frames) OVERRIDE FINAL;
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;

    /* override the time domain action, only for the general context */
    virtual bool getTimeDomain(OfxRangeD &range) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(ImageBlenderBase &, const RenderArguments &args, const SourceFrames &frames);

private:
    auto_ptr<Image> fetchDstImage(const RenderArguments &args);
    void getSourceFrames(double time, FieldEnum fieldToRender, SourceFrames *frames);
    std::vector<BoxSample> getBoxSamples(double time, double sourceTime, FieldEnum fieldToRender);
    double getSourceTime(double time);
    std::optional<double> getSpeedDuration(const OfxRangeD &srcRange);
};

// Box filter: accumulate weight * src into a float buffer that covers the render window.
// Pixels missing from src (outside its bounds, or no image) are transparent black.
template <class PIX, int nComponents>
class BoxAccumulator
    : public ImageProcessor
{
public:
    BoxAccumulator(ImageEffect &instance,
                   const Image *src,
                   float weight,
                   float *acc,
                   const OfxRectI &accBounds)
        : ImageProcessor(instance)
        , _src(src)
        , _weight(weight)
        , _acc(acc)
        , _accBounds(accBounds)
    {
    }

private:
    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        unused(rs);
        const std::size_t accWidth = (std::size_t)(_accBounds.x2 - _accBounds.x1);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }
            float *a = _acc + ( (std::size_t)(y - _accBounds.y1) * accWidth + (std::size_t)(procWindow.x1 - _accBounds.x1) ) * nComponents;
            for (int x = procWindow.x1; x < procWindow.x2; ++x, a += nComponents) {
                const PIX *p = (const PIX *)( _src ? _src->getPixelAddress(x, y) : 0 );
                if (p) {
                    for (int c = 0; c < nComponents; ++c) {
                        a[c] += _weight * (float)p[c];
                    }
                }
            }
        }
    }

    const Image *_src;
    const float _weight;
    float *_acc;
    const OfxRectI _accBounds;
};

// Box filter: write the float buffer that covers the render window to the destination image
// (integer depths: rounded to the nearest value, and clamped)
template <class PIX, int nComponents, int maxValue>
class BoxWriter
    : public ImageProcessor
{
public:
    BoxWriter(ImageEffect &instance,
              const float *acc,
              const OfxRectI &accBounds)
        : ImageProcessor(instance)
        , _acc(acc)
        , _accBounds(accBounds)
    {
    }

private:
    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        unused(rs);
        const std::size_t accWidth = (std::size_t)(_accBounds.x2 - _accBounds.x1);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }
            const float *a = _acc + ( (std::size_t)(y - _accBounds.y1) * accWidth + (std::size_t)(procWindow.x1 - _accBounds.x1) ) * nComponents;
            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            assert(dstPix);
            for (int x = procWindow.x1; x < procWindow.x2; ++x, a += nComponents, dstPix += nComponents) {
                for (int c = 0; c < nComponents; ++c) {
                    if (maxValue == 1) {
                        dstPix[c] = PIX(a[c]);
                    } else {
                        // the weights sum to 1, so a[c] is in [0, maxValue], up to rounding errors
                        const float v = std::floor(a[c] + 0.5f);
                        dstPix[c] = PIX( (v <= 0.f) ? 0.f : ( (v >= (float)maxValue) ? (float)maxValue : v ) );
                    }
                }
            }
        }
    }

    const float *_acc;
    const OfxRectI _accBounds;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

// make sure components are sane
static void
checkComponents(const Image &src,
                BitDepthEnum dstBitDepth,
                PixelComponentEnum dstComponents)
{
    BitDepthEnum srcBitDepth    = src.getPixelDepth();
    PixelComponentEnum srcComponents  = src.getPixelComponents();

    // see if they have the same depths and bytes and all
    if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
        throwSuiteStatusException(kOfxStatErrImageFormat);
    }
}

static void
framesNeeded(double sourceTime,
             FieldEnum fieldToRender,
             double *fromTimep,
             double *toTimep,
             double *blendp)
{
    // figure the two images we are blending between
    double fromTime, toTime;
    double blend;

    if (fieldToRender == eFieldNone) {
        // unfielded, easy peasy
        fromTime = std::floor(sourceTime);
        toTime = fromTime + 1;
        blend = sourceTime - fromTime;
    } else {
        // Fielded clips, pook. We are rendering field doubled images,
        // and so need to blend between fields, not frames.
        double frac = sourceTime - std::floor(sourceTime);
        if (frac < 0.5) {
            // need to go between the first and second fields of this frame
            fromTime = std::floor(sourceTime); // this will get the first field
            toTime   = fromTime + 0.5;    // this will get the second field of the same frame
            blend    = frac * 2.0;        // and the blend is between those two
        } else { // frac > 0.5
            fromTime = std::floor(sourceTime) + 0.5; // this will get the second field of this frame
            toTime   = std::floor(sourceTime) + 1.0; // this will get the first field of the next frame
            blend    = (frac - 0.5) * 2.0;
        }
    }
    *fromTimep = fromTime;
    *toTimep = toTime;
    *blendp = blend;
}

// the destination image, checked (never NULL: throws on failure)
auto_ptr<Image>
RetimePlugin::fetchDstImage(const RenderArguments &args)
{
    auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );

    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    if ( ( dst->getPixelDepth() != _dstClip->getPixelDepth() ) ||
         ( dst->getPixelComponents() != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        throwSuiteStatusException(kOfxStatFailed);
    }
# ifndef NDEBUG
    checkBadRenderScaleOrField(dst, args);
# endif

    return dst;
}

/* set up and run a processor */
void
RetimePlugin::setupAndProcess(ImageBlenderBase &processor,
                              const RenderArguments &args,
                              const SourceFrames &frames)
{
    const auto_ptr<Image> dst = fetchDstImage(args);
    const BitDepthEnum dstBitDepth = dst->getPixelDepth();
    const PixelComponentEnum dstComponents = dst->getPixelComponents();

    const bool srcConnected = _srcClip && _srcClip->isConnected();
    if (!frames.blended) {
        // should have been caught by isIdentity, unless the frame is black
        if (frames.fromBlack) {
            fillBlack( *this, args.renderWindow, args.renderScale, dst.get() );

            return;
        }
        auto_ptr<const Image> src( srcConnected ? _srcClip->fetchImage(frames.from) : 0 );
       if ( src.get() ) {
#    ifndef NDEBUG
            checkBadRenderScaleOrField(src, args);
#     endif
            BitDepthEnum srcBitDepth      = src->getPixelDepth();
            PixelComponentEnum srcComponents = src->getPixelComponents();
            if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
                throwSuiteStatusException(kOfxStatErrImageFormat);
            }
        }
        copyPixels( *this, args.renderWindow, args.renderScale, src.get(), dst.get() );

        return;
    }

    // fetch the two source images (a missing image is blended as transparent black)
    auto_ptr<Image> fromImg( (srcConnected && !frames.fromBlack) ?
                                  _srcClip->fetchImage(frames.from) : 0 );
    auto_ptr<Image> toImg( (srcConnected && !frames.toBlack) ?
                                _srcClip->fetchImage(frames.to) : 0 );

    // make sure bit depths are sane
    if ( fromImg.get() ) {
# ifndef NDEBUG
        checkBadRenderScaleOrField(fromImg, args);
# endif
        checkComponents(*fromImg, dstBitDepth, dstComponents);
    }
    if ( toImg.get() ) {
# ifndef NDEBUG
        checkBadRenderScaleOrField(toImg, args);
# endif
        checkComponents(*toImg, dstBitDepth, dstComponents);
    }

    // set the images
    processor.setDstImg( dst.get() );
    processor.setFromImg( fromImg.get() );
    processor.setToImg( toImg.get() );

    // set the render window
    processor.setRenderWindow(args.renderWindow, args.renderScale);

    // set the blend between
    processor.setBlend( (float)frames.blend );

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // RetimePlugin::setupAndProcess

// Box filter: average the frames of frames.box (a black frame, or a missing image, is transparent black)
template <class PIX, int nComponents, int maxValue>
void
RetimePlugin::renderBox(const RenderArguments &args,
                        const SourceFrames &frames)
{
    assert(frames.boxed);
    const auto_ptr<Image> dst = fetchDstImage(args);
    const OfxRectI& window = args.renderWindow;
    if ( (window.x2 <= window.x1) || (window.y2 <= window.y1) ) {
        return;
    }
    std::vector<float> acc( (std::size_t)(window.x2 - window.x1) * (std::size_t)(window.y2 - window.y1) * nComponents, 0.f );
    const bool srcConnected = _srcClip && _srcClip->isConnected();
    for (std::vector<BoxSample>::const_iterator it = frames.box.begin(); it != frames.box.end(); ++it) {
        if ( abort() ) {
            return;
        }
        if ( it->black() || !srcConnected ) {
            continue;
        }
        auto_ptr<const Image> src( _srcClip->fetchImage(it->time) );
        if ( !src.get() ) {
            continue;
        }
# ifndef NDEBUG
        checkBadRenderScaleOrField(src, args);
# endif
        checkComponents( *src, dst->getPixelDepth(), dst->getPixelComponents() );
        BoxAccumulator<PIX, nComponents> accumulator(*this, src.get(), (float)it->weight, &acc[0], window);
        accumulator.setRenderWindow(window, args.renderScale);
        accumulator.process();
    }
    BoxWriter<PIX, nComponents, maxValue> writer(*this, &acc[0], window);
    writer.setDstImg( dst.get() );
    writer.setRenderWindow(window, args.renderScale);
    writer.process();
}

// figure the source frame we should be retiming from at the given output time
double
RetimePlugin::getSourceTime(double time)
{
    if (getContext() == eContextRetimer) {
        // the host is specifying it, so fetch it from the kOfxImageEffectRetimerParamName pseudo-param
        return _sourceTime->getValueAtTime(time);
    }
    double sourceTime = time;
    if (_srcClip) {
        bool reverse_input;
        OfxRangeD srcRange = _srcClip->getFrameRange();
        _reverse_input->getValueAtTime(time, reverse_input);
        // we have our own param, which is a speed, so we integrate it to get the time we want
        if (reverse_input) {
            sourceTime = srcRange.max - _speed->integrate(srcRange.min, time);
        } else {
            sourceTime = srcRange.min + _speed->integrate(srcRange.min, time);
        }
        if (_warp) {
            double r = srcRange.max - srcRange.min;
            if (r != 0.) {
                sourceTime = srcRange.min + r * _warp->getValue(0, time, (sourceTime - srcRange.min) / r);
            }
        }
    }

    return sourceTime;
}

// The frames to read at the given output time: used by getFramesNeeded, isIdentity, getRegionOfDefinition
// and render, so that they all agree. Before/After is applied to each frame read, so that the Linear filter
// blends the right frames across the loop or bounce point.
void
RetimePlugin::getSourceFrames(double time,
                              FieldEnum fieldToRender,
                              SourceFrames *frames)
{
    const double sourceTime = getSourceTime(time);
    const FilterEnum filter = (FilterEnum)_filter->getValueAtTime(time);

    frames->blended = false;
    frames->blend = 0.;
    frames->boxed = false;
    frames->box.clear();
    if (filter == eFilterBox) {
        frames->box = getBoxSamples(time, sourceTime, fieldToRender);
        assert( !frames->box.empty() );
        frames->from = frames->box[0].time;
        frames->boxed = frames->box.size() > 1;
        if (!frames->boxed) {
            frames->box.clear();
        }
    } else if ( (sourceTime == (int)sourceTime) || (filter == eFilterNone) ) {
        frames->from = sourceTime;
    } else if (filter == eFilterNearest) {
        frames->from = std::floor(sourceTime + 0.5);
    } else {
        assert(filter == eFilterLinear);
        frames->blended = true;
        framesNeeded(sourceTime, fieldToRender, &frames->from, &frames->to, &frames->blend);
    }
    if (!frames->blended) {
        frames->to = frames->from;
    }

    BeforeAfterEnum fromMode = eBeforeAfterOriginal;
    BeforeAfterEnum toMode = eBeforeAfterOriginal;
    const OfxRangeD srcRange = ( _srcClip && _srcClip->isConnected() ) ? _srcClip->getFrameRange() : OfxRangeD();
    // only apply Before/After to a finite, reasonable source range
    if ( _srcClip && _srcClip->isConnected() &&
         (std::fabs(srcRange.min) < kMaxBeforeAfterFrame) && (std::fabs(srcRange.max) < kMaxBeforeAfterFrame) ) {
        const int first = (int)std::floor(srcRange.min);
        const int last = (int)std::ceil(srcRange.max);
        const BeforeAfterEnum before = (BeforeAfterEnum)_before->getValue();
        const BeforeAfterEnum after = (BeforeAfterEnum)_after->getValue();
        fromMode = mapSourceTime(first, last, before, after, &frames->from);
        toMode = mapSourceTime(first, last, before, after, &frames->to);
        for (std::vector<BoxSample>::iterator it = frames->box.begin(); it != frames->box.end(); ++it) {
            it->mode = mapSourceTime(first, last, before, after, &it->time);
        }
    }
    if (frames->boxed) {
        // merge the samples that read the same frame (e.g. Hold), and the black ones
        std::vector<BoxSample> merged;
        for (std::vector<BoxSample>::const_iterator it = frames->box.begin(); it != frames->box.end(); ++it) {
            const BoxSample &sample = *it;
            std::vector<BoxSample>::iterator m =
                std::find_if( merged.begin(), merged.end(), [&sample](const BoxSample &other) {
                    return sample.black() ? other.black() : ( !other.black() && (other.time == sample.time) );
                } );
            if ( m == merged.end() ) {
                merged.push_back(sample);
            } else {
                m->weight += sample.weight;
                if (m->mode == eBeforeAfterOriginal) {
                    m->mode = sample.mode; // e.g. Hold: the merged frame is held
                }
            }
        }
        frames->box.swap(merged);
        if (frames->box.size() == 1) {
            // a single frame: a copy (or black)
            frames->boxed = false;
            frames->from = frames->to = frames->box[0].time;
            fromMode = frames->box[0].mode;
            frames->box.clear();
        } else {
            frames->from = frames->to = frames->box[0].time;
            frames->fromBlack = frames->toBlack = false;
            frames->original = false;

            return;
        }
    }
    frames->fromBlack = (fromMode == eBeforeAfterBlack);
    frames->toBlack = frames->blended && (toMode == eBeforeAfterBlack);
    if ( frames->blended && !frames->fromBlack && !frames->toBlack && (frames->from == frames->to) ) {
        // e.g. Hold: do not blend an image with itself
        frames->blended = false;
    }
    frames->original = (fromMode == eBeforeAfterOriginal) && (!frames->blended || toMode == eBeforeAfterOriginal);
}

// Box filter: the source frames (or fields, at half-frame times) covered by the output frame (or
// field), from half a frame (or field) before the output time to half a frame (or field) after it,
// mapped to the source time. Source frame (or field) n covers the times n-1/2 to n+1/2 (in frames, or
// fields), and its weight is the part of the mapped interval that it covers. Before/After is not applied
// here. At least one sample, the one at sourceTime if the mapped interval is empty. In the Retimer
// context, the host gives the source time of the current time only: one sample, at sourceTime.
std::vector<BoxSample>
RetimePlugin::getBoxSamples(double time,
                            double sourceTime,
                            FieldEnum fieldToRender)
{
    const double step = (fieldToRender == eFieldNone) ? 1. : 0.5;
    std::vector<BoxSample> samples;
    double a = sourceTime;
    double b = sourceTime;
    if (getContext() != eContextRetimer) {
        a = getSourceTime(time - step / 2);
        b = getSourceTime(time + step / 2);
        if (b < a) {
            std::swap(a, b);
        }
    }
    // a finite, reasonable interval (also false for NaN)
    if ( (std::fabs(a) < kMaxBeforeAfterFrame) && (std::fabs(b) < kMaxBeforeAfterFrame) && (b > a) ) {
        if (b - a > kBoxMaxFrames * step) {
            const double middle = (a + b) / 2;
            a = middle - kBoxMaxFrames * step / 2;
            b = middle + kBoxMaxFrames * step / 2;
        }
        const double first = std::floor(a / step + 0.5);
        const double last = std::floor(b / step + 0.5);
        for (double n = first; n <= last; ++n) {
            const double overlap = (std::min)(b, (n + 0.5) * step) - (std::max)(a, (n - 0.5) * step);
            if (overlap > 0.) {
                const BoxSample sample = { n * step, overlap / (b - a), eBeforeAfterOriginal };
                samples.push_back(sample);
            }
        }
    }
    if ( samples.empty() ) {
        // speed 0, the Retimer context, or no reasonable interval: the frame (or field) at the source time
        const double n = (std::fabs(sourceTime) < kMaxBeforeAfterFrame) ? std::floor(sourceTime / step + 0.5) * step : sourceTime;
        const BoxSample sample = { n, 1., eBeforeAfterOriginal };
        samples.push_back(sample);
    }

    return samples;
}

void
RetimePlugin::getFramesNeeded(const FramesNeededArguments &args,
                              FramesNeededSetter &frames)
{
    if (!_srcClip || !_srcClip->isConnected()) {
        return;
    }
    // The rendered field is not known here: collect the frames read for unfielded and fielded renders.
    std::vector<double> times;
    const FieldEnum fields[2] = {eFieldNone, eFieldBoth};
    for (int i = 0; i < 2; ++i) {
        SourceFrames src;
        getSourceFrames(args.time, fields[i], &src);
        if (src.boxed) {
            for (std::vector<BoxSample>::const_iterator it = src.box.begin(); it != src.box.end(); ++it) {
                if ( !it->black() ) {
                    times.push_back(it->time);
                }
            }
            continue;
        }
        if (!src.fromBlack) {
            times.push_back(src.from);
        }
        if (src.blended && !src.toBlack) {
            times.push_back(src.to);
        }
    }
    // Times at most one frame apart make one continuous range, e.g. [n, n+1] as before Before/After
    // existed, which also covers the field n+0.5. A Loop/Bounce wrap gives separate ranges.
    // If every frame is black, nothing is requested and the host uses its default (the frame at args.time).
    std::sort( times.begin(), times.end() );
    const std::size_t n = times.size();
    for (std::size_t i = 0; i < n; ) {
        OfxRangeD range = {times[i], times[i]};
        while ( (++i < n) && (times[i] - range.max <= 1.) ) {
            range.max = times[i];
        }
        frames.setFramesNeeded(*_srcClip, range);
    }
}

bool
RetimePlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                    OfxRectD &rod)
{
    if (!_srcClip || !_srcClip->isConnected()) {
        return false;
    }
    SourceFrames src;
    getSourceFrames(args.time, eFieldNone, &src);
    if (src.boxed) {
        // the union of the RoDs of the frames read. The black samples are merged into one, so at
        // least one of the (two or more) samples is not black.
        bool first = true;
        for (std::vector<BoxSample>::const_iterator it = src.box.begin(); it != src.box.end(); ++it) {
            if ( it->black() ) {
                continue;
            }
            const OfxRectD frameRoD = _srcClip->getRegionOfDefinition(it->time, args.view);
            if (first) {
                rod = frameRoD;
                first = false;
            } else {
                Coords::rectBoundingBox(rod, frameRoD, &rod);
            }
        }
        assert(!first);

        return true;
    }
    if (src.blended && src.original) {
        // keep the host default, as before Before/After existed
        return false;
    }
    // union of the RoDs of the frames read, empty if they are all black
    rod.x1 = rod.y1 = rod.x2 = rod.y2 = 0.;
    if (!src.fromBlack) {
        rod = _srcClip->getRegionOfDefinition(src.from, args.view);
    }
    if (src.blended && !src.toBlack) {
        const OfxRectD toRoD = _srcClip->getRegionOfDefinition(src.to, args.view);
        if (src.fromBlack) {
            rod = toRoD;
        } else {
            Coords::rectBoundingBox(rod, toRoD, &rod);
        }
    }

    return true;
}

bool
RetimePlugin::isIdentity(const IsIdentityArguments &args,
                         Clip * &identityClip,
                         double &identityTime
                         , int& /*view*/, std::string& /*plane*/)
{
    if (!_srcClip || !_srcClip->isConnected()) {
        return false;
    }
    SourceFrames src;
    getSourceFrames(args.time, args.fieldToRender, &src);
    if (src.blended || src.boxed || src.fromBlack) {
        return false;
    }
    identityClip = _srcClip;
    identityTime = src.from;

    return true;
}

void
RetimePlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    // a black output outside of the input frame range varies from frame to frame, even with a still input
    if ( ( (BeforeAfterEnum)_before->getValue() == eBeforeAfterBlack ) ||
         ( (BeforeAfterEnum)_after->getValue() == eBeforeAfterBlack ) ) {
        clipPreferences.setOutputFrameVarying(true);
    }
}

// the furthest output duration that getSpeedDuration() considers, for an input frame range of the given
// length: a slower speed gives the host default. Hosts may evaluate an integral slowly (e.g. Natron
// integrates an expression numerically, frame by frame), and this is called from the host's frame range
// queries, so the search must stay short.
static double
maxSpeedDuration(double length)
{
    return (std::min)(length * 1000., 1e5);
}

// The output duration at the current speed: the largest whole number of frames d such that the
// signed integral I(d) of the speed, from the start of the input frame range to that start plus d,
// stays within the length of the input frame range, i.e. the time at which the last input frame is
// reached (forwards, from the first input frame; with Reverse input, backwards from the last one: the
// source time is first + I or last - I, in the input range for 0 <= I <= length). None (the host
// default) if the input range is empty, if the integral is negative (the speed plays the frames before
// the input, or after it with Reverse input), or if the input length is not reached within
// maxSpeedDuration() frames (a speed that is 0, too small or not finite).
// - A speed without keys (a constant, or an expression) is taken as constant, with its value at the
//   first input frame: d = length / speed.
// - An animated speed: d is searched by doubling, then by bisection on whole frames. This is exact
//   if the integral is monotonic (the speed does not change sign). If the speed changes sign, d is the
//   first probed duration where the integral reaches the length: frames where the integral is negative
//   or goes back may be included.
std::optional<double>
RetimePlugin::getSpeedDuration(const OfxRangeD &srcRange)
{
    const double length = srcRange.max - srcRange.min;
    if ( !(length > 0.) || !(length < kMaxBeforeAfterFrame) ) {
        return std::nullopt;
    }
    const double maxDuration = maxSpeedDuration(length);
    // a small tolerance, so that rounding errors do not drop the last frame
    const double target = length * (1. - 1e-9);
    const double limit = length * (1. + 1e-9);
    if (_speed->getNumKeys() == 0) {
        const double speed = _speed->getValueAtTime(srcRange.min);
        if ( !(speed > 0.) ) { // also NaN
            return std::nullopt;
        }
        const double d = length / speed;
        if ( !(d <= maxDuration) ) {
            return std::nullopt;
        }

        return std::floor(d * (1. + 1e-9));
    }
    double lo = 0.; // the integral is below the length after lo frames
    double hi = 1.;
    for (;;) {
        const double c = _speed->integrate(srcRange.min, srcRange.min + hi);
        if (c >= target) {
            break;
        }
        if ( !(c == c) || (hi >= maxDuration) ) { // NaN, or too slow (or negative)
            return std::nullopt;
        }
        lo = hi;
        hi = (std::min)(hi * 2, maxDuration);
    }
    // the integral reaches the length after hi frames, not after lo frames: the last whole frame not past it
    while (hi - lo > 1.) {
        const double mid = std::floor( (lo + hi) / 2 );
        if (_speed->integrate(srcRange.min, srcRange.min + mid) >= target) {
            hi = mid;
        } else {
            lo = mid;
        }
    }

    return (_speed->integrate(srcRange.min, srcRange.min + hi) <= limit) ? hi : lo;
}

/* override the time domain action, only for the general context */
bool
RetimePlugin::getTimeDomain(OfxRangeD &range)
{
    // kOfxImageEffectActionGetTimeDomain: "Only effects instantiated in the General Context can have
    // this called on them. In all other the host is in strict control over the temporal duration of the effect."
    if ( (getContext() != eContextGeneral) || !_srcClip || !_srcClip->isConnected() ) {
        return false;
    }
    // how many frames on the input clip
    const OfxRangeD srcRange = _srcClip->getFrameRange();
    if (_duration) {
        assert(!_warp);
        // Hosts without parametric parameters: the Duration param sets the duration of the effect
        // (it can not always be derived from the speed, e.g. one wired to an expression).
        double duration;
        _duration->getValue(duration); //don't animate

        range.min = srcRange.min;
        range.max = srcRange.min + (srcRange.max - srcRange.min) * duration;

        return true;
    }
    // From the speed: the output starts with the input (the speed is integrated from there) and ends
    // when the input frame range has been played (forwards, or backwards). The warp curve maps that
    // part of the output, from 0 to 1 on its X axis, so it does not change the range.
    if (_speed) {
        if ( const std::optional<double> duration = getSpeedDuration(srcRange) ) {
            range.min = srcRange.min;
            range.max = srcRange.min + *duration;

            return true;
        }
    }

    // the input range is empty, or the speed is 0, negative or too small: the default, i.e. the input time domain
    return false;
}

// the internal render function
template <int nComponents>
void
RetimePlugin::renderInternal(const RenderArguments &args,
                             const SourceFrames &frames,
                             BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case eBitDepthUByte: {
        if (frames.boxed) {
            renderBox<unsigned char, nComponents, 255>(args, frames);
            break;
        }
        ImageBlender<unsigned char, nComponents> fred(*this);
        setupAndProcess(fred, args, frames);
        break;
    }
    case eBitDepthUShort: {
        if (frames.boxed) {
            renderBox<unsigned short, nComponents, 65535>(args, frames);
            break;
        }
        ImageBlender<unsigned short, nComponents> fred(*this);
        setupAndProcess(fred, args, frames);
        break;
    }
    case eBitDepthFloat: {
        if (frames.boxed) {
            renderBox<float, nComponents, 1>(args, frames);
            break;
        }
        ImageBlender<float, nComponents> fred(*this);
        setupAndProcess(fred, args, frames);
        break;
    }
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
RetimePlugin::render(const RenderArguments &args)
{
    const double time = args.time;
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );

    // figure the frames we should be retiming from
    SourceFrames frames;
    getSourceFrames(time, args.fieldToRender, &frames);

#ifdef DEBUG
    if (!frames.blended && !frames.boxed && !frames.fromBlack) {
        // should be caught by isIdentity!
        setPersistentMessage(Message::eMessageError, "", "OFX Host should not render");
        throwSuiteStatusException(kOfxStatFailed);
    }
#endif

    // do the rendering
    if (dstComponents == ePixelComponentRGBA) {
        renderInternal<4>(args, frames, dstBitDepth);
    } else if (dstComponents == ePixelComponentRGB) {
        renderInternal<3>(args, frames, dstBitDepth);
#ifdef OFX_EXTENSIONS_NATRON
    } else if (dstComponents == ePixelComponentXY) {
        renderInternal<2>(args, frames, dstBitDepth);
#endif
    } else {
        assert(dstComponents == ePixelComponentAlpha);
        renderInternal<1>(args, frames, dstBitDepth);
    }
} // RetimePlugin::render

mDeclarePluginFactory(RetimePluginFactory,; , {});
void
RetimePluginFactory::load()
{
    ofxsThreadSuiteCheck();
    // we can't be used on hosts that don't perfrom temporal clip access
    if (!getImageEffectHostDescription()->temporalClipAccess) {
        throw Exception::HostInadequate("Need random temporal image access to work");
    }
}

/** @brief The basic describe function, passed a plugin descriptor */
void
RetimePluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // Say we are a transition context
    desc.addSupportedContext(eContextRetimer);
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);

    // Add supported pixel depths
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(true); // say we will be doing random time access on clips
    desc.setRenderTwiceAlways(true); // each field has to be rendered separately, since it may come from a different time
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);

    // we can't be used on hosts that don't perfrom temporal clip access
    if (!getImageEffectHostDescription()->temporalClipAccess) {
        throw Exception::HostInadequate("Need random temporal image access to work");
    }
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void
RetimePluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                       ContextEnum context)
{
    // we are a transition, so define the sourceTo input clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    srcClip->addSupportedComponent(ePixelComponentXY);
#endif
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(true); // say we will be doing random time access on this clip
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setFieldExtraction(eFieldExtractDoubled); // which is the default anyway

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    dstClip->addSupportedComponent(ePixelComponentXY);
#endif
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setFieldExtraction(eFieldExtractDoubled); // which is the default anyway
    dstClip->setSupportsTiles(kSupportsTiles);

    // make a page to put it in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // what param we have is dependant on the host
    if (context == eContextRetimer) {
        // Define the mandated kOfxImageEffectRetimerParamName param, note that we don't do anything with this other than.
        // describe it. It is not a true param but how the host indicates to the plug-in which frame
        // it wants you to retime to. It appears on no plug-in side UI, it is purely the host's to manage.
        DoubleParamDescriptor *param = desc.defineDoubleParam(kOfxImageEffectRetimerParamName);
        (void)param;
    }  else {
        // We are a general or filter context, define a speed param and a page of controls to put that in
        // reverse_input
        {
            BooleanParamDescriptor *param = desc.defineBooleanParam(kParamReverseInput);
            param->setDefault(false);
            param->setHint(kParamReverseInputHint);
            param->setLabel(kParamReverseInputLabel);
            param->setAnimates(true);
            if (page) {
                page->addChild(*param);
            }
        }

        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSpeed);
            param->setLabel(kParamSpeedLabel);
            param->setHint(kParamSpeedHint);
            param->setDefault(1);
            param->setIncrement(0.05);
            param->setRange(-DBL_MAX, DBL_MAX);
            param->setDisplayRange(0.1, 10.);
            param->setAnimates(true); // can animate
            param->setCacheInvalidation(eCacheInvalidateValueChangeToEnd); // any speed change affects all frames to end of sequence
            param->setDoubleType(eDoubleTypeScale);
            if (page) {
                page->addChild(*param);
            }
        }

        const ImageEffectHostDescription &hostDescription = *getImageEffectHostDescription();
        const bool supportsParametricParameter = ( hostDescription.supportsParametricParameter &&
                                                   !(hostDescription.hostName == "uk.co.thefoundry.nuke" &&
                                                     8 <= hostDescription.versionMajor && hostDescription.versionMajor <= 11) );  // Nuke 8-11.1 are known to *not* support Parametric
        if (supportsParametricParameter) {
            PageParamDescriptor* page = desc.definePageParam(kPageTimeWarp);
            if (page) {
                page->setLabel(kPageTimeWarpLabel);
            }
            {
                ParametricParamDescriptor* param = desc.defineParametricParam(kParamWarp);
                assert(param);
                param->setLabel(kParamWarpLabel);
                param->setHint(kParamWarpHint);

                // define it as one dimensional
                param->setDimension(1);
                param->setDimensionLabel(kParamWarp, 0);

                const OfxRGBColourD blue  = {0.5, 0.5, 1};      //set blue color to blue curve
                param->setUIColour( 0, blue );

                // set the min/max parametric range to 0..1
                param->setRange(0.0, 1.0);
                // set the default Y range to 0..1 for all dimensions
                param->setDimensionDisplayRange(0., 1., 0);

                param->setIdentity(0);

                // add param to page
                if (page) {
                    page->addChild(*param);
                }
            }
        } else if (context == eContextGeneral) {
            // If we are a general context, we can change the duration of the effect, so have a param to do that
            // We need a separate param as it is impossible to derive this from a speed param and the input clip
            // duration (the speed may be animating or wired to an expression).

            // This is not possible if there's a warp curve.

            // We are a general or filter context, define a speed param and a page of controls to put that in
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamDuration);
            param->setLabel(kParamDurationLabel);
            param->setHint(kParamDurationHint);
            param->setDefault(1);
            param->setIncrement(0.1);
            param->setRange(0, 10);
            param->setDisplayRange(0, 10);
            param->setAnimates(false); // used in getTimeDomain()
            param->setDoubleType(eDoubleTypeScale);

            // add param to page
            if (page) {
                page->addChild(*param);
            }
        }
    }

    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamFilter);
        param->setLabel(kParamFilterLabel);
        param->setHint(kParamFilterHint);
        assert(param->getNOptions() == eFilterNone);
        param->appendOption(kParamFilterOptionNone);
        assert(param->getNOptions() == eFilterNearest);
        param->appendOption(kParamFilterOptionNearest);
        assert(param->getNOptions() == eFilterLinear);
        param->appendOption(kParamFilterOptionLinear);
        assert(param->getNOptions() == eFilterBox);
        param->appendOption(kParamFilterOptionBox);
        param->setDefault( (int)kParamFilterDefault );
        if (page) {
            page->addChild(*param);
        }
    }
    // Original (the default) asks the input for frames outside its range, as before these params existed
    defineBeforeAfterParam(desc, page, kParamBefore, kParamBeforeLabel, kParamBeforeHint, eBeforeAfterOriginal);
    defineBeforeAfterParam(desc, page, kParamAfter, kParamAfterLabel, kParamAfterHint, eBeforeAfterOriginal);
} // RetimePluginFactory::describeInContext

/** @brief The create instance function, the plugin must return an object derived from the \ref ImageEffect class */
ImageEffect*
RetimePluginFactory::createInstance(OfxImageEffectHandle handle,
                                    ContextEnum /*context*/)
{
    const ImageEffectHostDescription &hostDescription = *getImageEffectHostDescription();
    const bool supportsParametricParameter = ( hostDescription.supportsParametricParameter &&
                                               !(hostDescription.hostName == "uk.co.thefoundry.nuke" &&
                                                 8 <= hostDescription.versionMajor && hostDescription.versionMajor <= 11) );  // Nuke 8-11.1 are known to *not* support Parametric

    return new RetimePlugin(handle, supportsParametricParameter);
}

static RetimePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
