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
   - selection of the integration filter (box or nearest) and shutter time
   - handle fielded input correctly

   - retiming based on optical flow computation will be done separately
 */

#include <cmath> // for floor
#include <cfloat> // DBL_MAX
#include <cassert>
#include <climits> // INT_MAX
#include <algorithm> // std::sort

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
    "The Before and After parameters set what is read when the source frame falls outside the frame range of the input clip: the input frame itself (Original), the first or last frame (Hold), transparent black (Black), or frames repeated from the input range (Loop, Bounce).\n" \
    "Before and After refer to the source frame range, not to the output: when the input is played backwards (Reverse input, or a negative speed or warp slope), Before sets what is shown at the end of the output.\n" \
    "See also: https://web.archive.org/web/20220627032808/http://www.opticalenquiry.com/nuke/index.php?title=Retime"

#define kPluginIdentifier "net.sf.openfx.Retime"
// History:
// version 1.0: initial version
// version 1.1: a NULL Source image (disconnected input; render is called although isIdentity returned true,
//              e.g. by hostDemo) renders black instead of crashing (SupportExt copyPixels)
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
#define kParamDurationHint "How long the output clip should be, as a proportion of the input clip's length."

#define kParamFilter "filter"
#define kParamFilterLabel "Filter"
#define kParamFilterHint "How input images are combined to compute the output image."

#define kParamFilterOptionNone "None", "Do not interpolate, ask for images with fractional time to the input effect. Useful if the input effect can interpolate itself.", "none"
#define kParamFilterOptionNearest "Nearest", "Pick input image with nearest integer time.", "nearest"
#define kParamFilterOptionLinear "Linear", "Blend the two nearest images with linear interpolation.", "linear"
// TODO:
#define kParamFilterOptionBox "Box", "Weighted average of images over the shutter time (shutter time is defined in the output sequence).", "box" // requires shutter parameter

enum FilterEnum
{
    eFilterNone,
    eFilterNearest,
    eFilterLinear,
    //eFilterBox,
};

#define kParamFilterDefault eFilterLinear

#define kParamBeforeHint "What to read when the source frame (computed from the retiming parameters) is before the first frame of the input clip. This refers to source frames: when the input is played backwards, it applies at the end of the output."
#define kParamAfterHint "What to read when the source frame (computed from the retiming parameters) is after the last frame of the input clip. This refers to source frames: when the input is played backwards, it applies at the start of the output."

#define kPageTimeWarp "timeWarp"
#define kPageTimeWarpLabel "Time Warp"

#define kParamWarp "warp"
#define kParamWarpLabel "Warp"
#define kParamWarpHint "Curve that maps input range (after applying speed) to the output range. A low positive slope slows down the input clip, and a negative slope plays it backwards."


// The source frames used at a given output time, after applying Before/After.
struct SourceFrames
{
    double from, to; // source times of the frames to read
    bool fromBlack, toBlack; // the frame is transparent black (Before/After = Black)
    bool original; // no Before/After behaviour applies: keeps old projects rendering as before these params existed
    bool blended; // blend "from" and "to" (Linear filter), else only "from" is used
    double blend; // weight of "to"
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
    void getSourceFrames(double time, FieldEnum fieldToRender, SourceFrames *frames);
    double getSourceTime(double time);
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

/* set up and run a processor */
void
RetimePlugin::setupAndProcess(ImageBlenderBase &processor,
                              const RenderArguments &args,
                              const SourceFrames &frames)
{
    const double time = args.time;

    // get a dst image
    auto_ptr<Image>  dst( _dstClip->fetchImage(time) );

    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        throwSuiteStatusException(kOfxStatFailed);
    }
# ifndef NDEBUG
    checkBadRenderScaleOrField(dst, args);
# endif

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
    if ( (sourceTime == (int)sourceTime) || (filter == eFilterNone) ) {
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
    }
    frames->fromBlack = (fromMode == eBeforeAfterBlack);
    frames->toBlack = frames->blended && (toMode == eBeforeAfterBlack);
    if ( frames->blended && !frames->fromBlack && !frames->toBlack && (frames->from == frames->to) ) {
        // e.g. Hold: do not blend an image with itself
        frames->blended = false;
    }
    frames->original = (fromMode == eBeforeAfterOriginal) && (!frames->blended || toMode == eBeforeAfterOriginal);
}

void
RetimePlugin::getFramesNeeded(const FramesNeededArguments &args,
                              FramesNeededSetter &frames)
{
    if (!_srcClip || !_srcClip->isConnected()) {
        return;
    }
    // The rendered field is not known here: collect the frames read for unfielded and fielded renders.
    double times[4];
    int n = 0;
    const FieldEnum fields[2] = {eFieldNone, eFieldBoth};
    for (int i = 0; i < 2; ++i) {
        SourceFrames src;
        getSourceFrames(args.time, fields[i], &src);
        if (!src.fromBlack) {
            times[n++] = src.from;
        }
        if (src.blended && !src.toBlack) {
            times[n++] = src.to;
        }
    }
    // Times at most one frame apart make one continuous range, e.g. [n, n+1] as before Before/After
    // existed, which also covers the field n+0.5. A Loop/Bounce wrap gives separate ranges.
    // If every frame is black, nothing is requested and the host uses its default (the frame at args.time).
    std::sort(times, times + n);
    for (int i = 0; i < n; ) {
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
    if (src.blended || src.fromBlack) {
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

/* override the time domain action, only for the general context */
bool
RetimePlugin::getTimeDomain(OfxRangeD &range)
{
    // this should only be called in the general context, ever!
    if ( (getContext() == eContextGeneral) && _srcClip && _duration ) {
        assert(!_warp);
        // If we are a general context, we can changed the duration of the effect, so have a param to do that
        // We need a separate param as it is impossible to derive this from a speed param and the input clip
        // duration (the speed may be animating or wired to an expression).
        double duration;
        _duration->getValue(duration); //don't animate

        // how many frames on the input clip
        OfxRangeD srcRange = _srcClip->getFrameRange();

        range.min = srcRange.min;
        range.max = srcRange.min + (srcRange.max - srcRange.min) * duration;

        return true;
    }

    // If there's a warp curve, the time domain could be determined from the intersections of the warp curve with y=0 and y=1.

    // for now, we prefer returning the input time domain.
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
        ImageBlender<unsigned char, nComponents> fred(*this);
        setupAndProcess(fred, args, frames);
        break;
    }
    case eBitDepthUShort: {
        ImageBlender<unsigned short, nComponents> fred(*this);
        setupAndProcess(fred, args, frames);
        break;
    }
    case eBitDepthFloat: {
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
    if (!frames.blended && !frames.fromBlack) {
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
        //assert(param->getNOptions() == eFilterBox);
        //param->appendOption(kParamFilterOptionBox, kParamFilterOptionBoxHint);
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
