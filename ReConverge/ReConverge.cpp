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
 * OFX ReConverge plugin.
 * Shift convergence so that a tracked point appears at screen-depth.
 * The ReConverge node only shifts views horizontally, not vertically.
 */

#include <algorithm> // std::copy, std::fill, std::max, std::min
#include <atomic>
#include <cfloat> // DBL_MAX
#include <cmath> // std::lround
#include <memory> // std::unique_ptr
#include <optional>
#include <string>

#include "ofxsProcessing.H"
#include "ofxsPositionInteract.h"
#include "ofxsMacros.h"
#include "ofxsCoords.h"
#include "ofxsThreadSuite.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "ReConvergeOFX"
#define kPluginGrouping "Views/Stereo"
#define kPluginDescription \
    "Shift convergence so that a tracked point appears at screen-depth.\n" \
    "The views are shifted horizontally so that the point at \"Converge Upon\" gets a zero disparity " \
    "(it appears at screen depth), and then by the Convergence Offset. Mode selects the view that is shifted.\n" \
    "The horizontal disparity is read in the first channel of the Disparity input (red, or alpha if it " \
    "only has alpha), in the left view, at the Converge Upon point (at the nearest edge pixel if the point " \
    "is outside the disparity image). It is the position of the matching point in the right view minus " \
    "its position in the left view, in pixels at full resolution. The disparity must be a float image (all the clips then get the float depth): an 8- or " \
    "16-bit disparity cannot hold a disparity in pixels, so it is ignored (with a warning). Without a " \
    "Disparity input, only the offset is applied.\n" \
    "The disparity and the offset are each truncated to an integer number of pixels (in " \
    "\"Shift Both\" mode, each half is truncated), so that the image is never resampled. The pixels shifted " \
    "in are black and transparent, and the region of definition is the one of the source.\n" \
    "The views are identified by their index: the first view is the left view and the second " \
    "one the right view. Shift Right only shifts the right view, Shift Left only the left view, and Shift " \
    "Both shifts the left view one way and every other view the other way. On a host without multiview, " \
    "the only view is the left view.\n" \
    "At a render scale below 1 (proxy), the shift is scaled and rounded to the nearest pixel. Only the " \
    "Source channels are output: other planes, such as a disparity, are not shifted."
#define kPluginIdentifier "net.sf.openfx.reConvergePlugin"
// version 1.0: the first working version: the offset became a double param (truncated in
// render), and the converge point defaults to (0,0). An early,
// unfinished version 1.0 (with an Int offset) was built in release in 2013-14, before Natron 1.0;
// it was then only built in Debug.
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamConvergePoint "convergePoint"
#define kParamConvergePointLabel "Converge Upon"
#define kParamConvergePointHint \
    "The point that appears at screen depth: the views are shifted so that its disparity is zero. " \
    "Link it to a tracker to keep a moving element at screen depth. Only used with a Disparity input " \
    "(in the general context)."

#define kParamInteractive "interactive"
#define kParamInteractiveLabel "Interactive"
#define kParamInteractiveHint \
    "When checked the image will be rendered whenever moving the overlay interact instead of when releasing the mouse button."

#define kParamOffset "offset"
#define kParamOffsetLabel "Convergence Offset"
#define kParamOffsetHint \
    "Disparity added after the convergence, in pixels at full resolution (truncated to an integer). " \
    "Positive values bring the elements forward from the screen, negative values push them back."

#define kParamConvergeMode "convergeMode"
#define kParamConvergeModeLabel "Mode"
#define kParamConvergeModeHint "The view that is shifted to set the convergence."
#define kParamConvergeModeOptionShiftRight "Shift Right", "Shift the right view.", "right"
#define kParamConvergeModeOptionShiftLeft "Shift Left", "Shift the left view.", "left"
#define kParamConvergeModeOptionShiftBoth "Shift Both", "Shift the left view one way and the other views the other way, by half the shift each.", "both"

// the options of kParamConvergeMode, in order
enum ConvergeModeEnum
{
    eConvergeModeShiftRight = 0,
    eConvergeModeShiftLeft,
    eConvergeModeShiftBoth,
};

#define kClipDisparity "Disparity"

// The role of a view, by its index: the first view is the left view, the second
// one the right view. Shift Right only shifts the right view, Shift Left only the left view, and
// Shift Both shifts the left view one way and every other view the other way.
enum class ViewRole
{
    left,
    right,
    other, // a third view or more
};

constexpr int kLeftView = 0; // the index of the left view, where the disparity is read
constexpr int kRightView = 1;

ViewRole
viewRole(int view)
{
    return view == kLeftView ? ViewRole::left : (view == kRightView ? ViewRole::right : ViewRole::other);
}

// shifts are clamped to +-kMaxShift pixels: more than any image width, and far from the int limits
constexpr int kMaxShift = 1 << 24;

// v truncated towards zero, as the disparity and the offset are (NaN gives 0)
int
truncToShift(double v)
{
    if ( OFX::IsNaN(v) ) {
        return 0;
    }

    return static_cast<int>( std::max( -double(kMaxShift), std::min(double(kMaxShift), v) ) );
}

// The horizontal shift of a view, in pixels at full resolution: the output pixel x is the source
// pixel x + shift. The disparity and the offset are truncated separately, and in "Shift Both"
// mode each half is truncated (a disparity of 11 shifts each view by 5 pixels).
int
viewShift(ConvergeModeEnum mode,
          ViewRole role,
          double disparity,
          double offset)
{
    switch (mode) {
    case eConvergeModeShiftRight:

        return role == ViewRole::right ? truncToShift(disparity) + truncToShift(offset) : 0;
    case eConvergeModeShiftLeft:

        return role == ViewRole::left ? -( truncToShift(disparity) + truncToShift(offset) ) : 0;
    case eConvergeModeShiftBoth: {
        const int half = truncToShift(disparity / 2) + truncToShift(offset / 2);

        return role == ViewRole::left ? -half : half;
    }
    }

    return 0;
}

// Is the view passed to isIdentity and getRegionsOfInterest? Hosts with the Nuke multiview plane
// suite (V2) pass it (the plug-in is view aware), and hosts without views (OFX 1.4: neither that
// suite nor the Vegas stereoscopic suite) only have view 0. A Vegas host may not pass it.
// Computed once: fetchSuite logs the missing suites.
bool
hostPassesViewToActions()
{
    static const bool passesView = fetchSuite(kFnOfxImageEffectPlaneSuite, 2, true) ||
                                   !fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true);

    return passesView;
}

// The pixel, at the render scale, where the disparity is read: the one that contains the converge
// point, or the nearest one in dispBounds (the pixels of the disparity RoD) if the point is
// outside. None if dispBounds is empty.
std::optional<OfxPointI>
convergePixel(const OfxPointD& convergePoint,
              const OfxRectI& dispBounds,
              const OfxPointD& renderScale,
              double par)
{
    if ( Coords::rectIsEmpty(dispBounds) ) {
        return std::nullopt;
    }
    OfxPointI pixel;
    Coords::toPixel(convergePoint, renderScale, par, &pixel);
    pixel.x = std::max( dispBounds.x1, std::min(dispBounds.x2 - 1, pixel.x) );
    pixel.y = std::max( dispBounds.y1, std::min(dispBounds.y2 - 1, pixel.y) );

    return pixel;
}

// The disparity at pixel p of a float disparity image: its first component (alpha for an Alpha
// image), 0 outside the image.
double
disparityAt(const Image& disp,
            const OfxPointI& p)
{
    assert(disp.getPixelDepth() == eBitDepthFloat);
    const float* pix = static_cast<const float*>( disp.getPixelAddress(p.x, p.y) );

    return pix ? *pix : 0.;
}

// Base class of the processors: the output pixel (x, y) is the source pixel (x + shift, y), or
// black and transparent outside the source image.
class ShiftBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg = nullptr;
    int _shift = 0;

public:
    explicit ShiftBase(ImageEffect &instance)
        : ImageProcessor(instance)
    {
    }

    void setSrcImg(const Image *v) { _srcImg = v; }

    void setShift(int shift) { _shift = shift; }
};

template <class PIX, int nComponents>
class ImageShifter
    : public ShiftBase
{
public:
    explicit ImageShifter(ImageEffect &instance)
        : ShiftBase(instance)
    {
    }

private:
    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        unused(rs);
        // [x1, x2): the output columns whose source column x + _shift is in the source image
        int x1 = procWindow.x1;
        int x2 = procWindow.x1;
        if (_srcImg) {
            const OfxRectI& srcBounds = _srcImg->getBounds();
            x1 = std::max(procWindow.x1, srcBounds.x1 - _shift);
            x2 = std::min(procWindow.x2, srcBounds.x2 - _shift);
            if (x2 <= x1) {
                x1 = x2 = procWindow.x1;
            }
        }
        const int width = procWindow.x2 - procWindow.x1;
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = static_cast<PIX *>( _dstImg->getPixelAddress(procWindow.x1, y) );
            assert(dstPix);
            if (!dstPix) {
                continue;
            }
            const PIX *srcPix = (x1 < x2) ? static_cast<const PIX *>( _srcImg->getPixelAddress(x1 + _shift, y) ) : nullptr;
            if (!srcPix) {
                // no source pixel on this row: black and transparent
                std::fill(dstPix, dstPix + width * nComponents, PIX());
                continue;
            }
            dstPix = std::fill_n(dstPix, (x1 - procWindow.x1) * nComponents, PIX());
            dstPix = std::copy(srcPix, srcPix + (x2 - x1) * nComponents, dstPix);
            std::fill_n(dstPix, (procWindow.x2 - x2) * nComponents, PIX());
        }
    }
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ReConvergePlugin
    : public ImageEffect
{
public:
    explicit ReConvergePlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentAlpha ||
                             _dstClip->getPixelComponents() == ePixelComponentRGB ||
#ifdef OFX_EXTENSIONS_NATRON
                             _dstClip->getPixelComponents() == ePixelComponentXY ||
#endif
                             _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(_srcClip);
        // the Disparity clip only exists in the general context
        _dispClip = (getContext() == eContextGeneral) ? fetchClip(kClipDisparity) : nullptr;
        _convergePoint = fetchDouble2DParam(kParamConvergePoint);
        _interactive = fetchBooleanParam(kParamInteractive);
        _offset = fetchDoubleParam(kParamOffset);
        _convergeMode = fetchChoiceParam(kParamConvergeMode);
        assert(_convergePoint && _interactive && _offset && _convergeMode);
        updateConvergePointEnabled();
    }

private:
    void render(const RenderArguments &args) OVERRIDE FINAL;

    bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime
#ifdef OFX_EXTENSIONS_NUKE
                    , int& view, std::string& plane
#endif
                    ) OVERRIDE FINAL;

    void getRegionsOfInterest(const RegionsOfInterestArguments &args, RegionOfInterestSetter &rois) OVERRIDE FINAL;

#ifdef OFX_EXTENSIONS_NUKE
    void getFrameViewsNeeded(const FrameViewsNeededArguments& args, FrameViewsNeededSetter& frameViews) OVERRIDE FINAL;
#endif

    void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    template <int nComponents>
    void renderInternal(const RenderArguments &args, BitDepthEnum dstBitDepth);

    void setupAndProcess(ShiftBase &, const RenderArguments &args);

    bool hasDisparity() const { return _dispClip && _dispClip->isConnected(); }

    // Converge Upon (and its overlay) is only used with a Disparity input. In the filter context,
    // which has none, it is also hidden (describeInContext).
    void updateConvergePointEnabled()
    {
        _convergePoint->setEnabled( hasDisparity() );
        _interactive->setEnabled( hasDisparity() );
    }

    ConvergeModeEnum getConvergeMode(double time) const
    {
        return static_cast<ConvergeModeEnum>( _convergeMode->getValueAtTime(time) );
    }

    // the disparity RoD in the left view, in pixels at the render scale
    OfxRectI getDisparityBounds(double time, const OfxPointD& renderScale) const;

    // the horizontal disparity at the converge point, in the left view (0 without a disparity)
    double fetchDisparity(const RenderArguments &args);

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip = nullptr;
    Clip *_srcClip = nullptr;
    Clip *_dispClip = nullptr;
    Double2DParam *_convergePoint = nullptr;
    BooleanParam *_interactive = nullptr;
    DoubleParam *_offset = nullptr;
    ChoiceParam *_convergeMode = nullptr;
    // the 8- or 16-bit disparity warning is shown (renders may run concurrently)
    std::atomic<bool> _disparityWarned{false};
};

// a shift in pixels at full resolution, at the render scale: rounded half away from zero, so that
// both views of "Shift Both" stay symmetric. The pixel aspect ratio does not matter, since the
// disparity and the offset are in pixels too.
int
scaledShift(int shift,
            const OfxPointD& renderScale)
{
    return static_cast<int>( std::lround(shift * renderScale.x) );
}

OfxRectI
ReConvergePlugin::getDisparityBounds(double time,
                                     const OfxPointD& renderScale) const
{
    assert(_dispClip);
#ifdef OFX_EXTENSIONS_NUKE
    const OfxRectD dispRoD = _dispClip->getRegionOfDefinition(time, kLeftView);
#else
    const OfxRectD dispRoD = _dispClip->getRegionOfDefinition(time);
#endif
    OfxRectI bounds;
    Coords::toPixelEnclosing(dispRoD, renderScale, _dispClip->getPixelAspectRatio(), &bounds);

    return bounds;
}

double
ReConvergePlugin::fetchDisparity(const RenderArguments &args)
{
    if ( !hasDisparity() ) {
        return 0.;
    }
    // The clip depth is known before fetching (it is the one of the clip preferences). With
    // kSupportsMultipleClipDepths false, the host maps every clip to the deepest depth of the
    // input clips (ofxClipPreferences.rst), so a float disparity makes all the clips float. The
    // disparity is only 8- or 16-bit if it is itself 8- or 16-bit (or on a host that does not
    // follow that rule): that can't hold a disparity in pixels (negative, or above 1), so ignore
    // it, and say so (once: renders may run concurrently).
    if ( _dispClip->getPixelDepth() != eBitDepthFloat ) {
        if ( !_disparityWarned.exchange(true) ) {
            setPersistentMessage(Message::eMessageWarning, "",
                                 "The disparity is ignored: it is only read from float images, and the images are 8- or 16-bit.");
        }

        return 0.;
    }
    if ( _disparityWarned.exchange(false) ) {
        clearPersistentMessage(); // only our 8- or 16-bit warning was shown
    }
    // the left view, whatever the rendered view (on a host without views, fetchStereoscopicImage
    // fetches the only view)
    std::unique_ptr<const Image> disp( _dispClip->fetchStereoscopicImage(args.time, kLeftView) );
    if ( !disp.get() ) {
        return 0.;
    }
    checkBadRenderScaleOrField(disp, args);
    if ( disp->getPixelDepth() != eBitDepthFloat ) {
        throwSuiteStatusException(kOfxStatErrImageFormat);
    }
    const std::optional<OfxPointI> p = convergePixel(_convergePoint->getValueAtTime(args.time),
                                                     getDisparityBounds(args.time, args.renderScale),
                                                     args.renderScale, _dispClip->getPixelAspectRatio());

    return p ? disparityAt(*disp, *p) : 0.;
}

/* set up and run a processor */
void
ReConvergePlugin::setupAndProcess(ShiftBase &processor,
                                  const RenderArguments &args)
{
    std::unique_ptr<Image> dst( _dstClip->fetchImage(args.time) );

    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    const BitDepthEnum dstBitDepth = dst->getPixelDepth();
    const PixelComponentEnum dstComponents = dst->getPixelComponents();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        throwSuiteStatusException(kOfxStatFailed);
    }
    checkBadRenderScaleOrField(dst, args);

    // the source image, in the rendered view
    std::unique_ptr<const Image> src( _srcClip->isConnected() ? _srcClip->fetchImage(args.time) : nullptr );
    if ( src.get() ) {
        checkBadRenderScaleOrField(src, args);
        if ( (src->getPixelDepth() != dstBitDepth) || (src->getPixelComponents() != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

#if defined(OFX_EXTENSIONS_VEGAS) || defined(OFX_EXTENSIONS_NUKE)
    const int view = args.renderView;
#else
    const int view = kLeftView;
#endif
    const int shift = viewShift( getConvergeMode(args.time), viewRole(view), fetchDisparity(args),
                                 _offset->getValueAtTime(args.time) );

    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );
    processor.setRenderWindow(args.renderWindow, args.renderScale);
    processor.setShift( scaledShift(shift, args.renderScale) );
    processor.process();
} // ReConvergePlugin::setupAndProcess

bool
ReConvergePlugin::isIdentity(const IsIdentityArguments &args,
                             Clip * &identityClip,
                             double & /*identityTime*/
#ifdef OFX_EXTENSIONS_NUKE
                             , int& /*view*/
                             , std::string& /*plane*/
#endif
                             )
{
    // Images can only be fetched in render, not here: only the views that are not shifted
    // whatever the disparity, and the shifts that are zero without a disparity, are identities.
    const ConvergeModeEnum mode = getConvergeMode(args.time);
    const double offset = _offset->getValueAtTime(args.time);
    bool identity = !hasDisparity() &&
                    viewShift(mode, ViewRole::left, 0., offset) == 0 &&
                    viewShift(mode, ViewRole::right, 0., offset) == 0 &&
                    viewShift(mode, ViewRole::other, 0., offset) == 0;
#ifdef OFX_EXTENSIONS_NUKE
    if ( !identity && hostPassesViewToActions() ) {
        const ViewRole role = viewRole(args.view);
        identity = (mode == eConvergeModeShiftRight && role != ViewRole::right) ||
                   (mode == eConvergeModeShiftLeft && role != ViewRole::left);
    }
#endif
    if (identity) {
        identityClip = _srcClip;
    }

    return identity;
}

void
ReConvergePlugin::getRegionsOfInterest(const RegionsOfInterestArguments &args,
                                       RegionOfInterestSetter &rois)
{
#ifdef OFX_EXTENSIONS_NUKE
    if ( !hasDisparity() && hostPassesViewToActions() ) {
        // the shift is known: the render window, shifted
        const int shift = viewShift( getConvergeMode(args.time), viewRole(args.view), 0., _offset->getValueAtTime(args.time) );
        const double dx = scaledShift(shift, args.renderScale) * _srcClip->getPixelAspectRatio() / args.renderScale.x;
        OfxRectD srcRoI = args.regionOfInterest;
        srcRoI.x1 += dx;
        srcRoI.x2 += dx;
        rois.setRegionOfInterest(*_srcClip, srcRoI);

        return;
    }
#endif
    // The shift depends on the disparity, which can only be read in render: the source RoI is the
    // whole horizontal band of the source RoD. (getRegionsOfInterest is not view-specific on every
    // host either: this band is right for every view.)
#ifdef OFX_EXTENSIONS_NUKE
    const OfxRectD srcRoD = _srcClip->getRegionOfDefinition(args.time, args.view);
#else
    const OfxRectD srcRoD = _srcClip->getRegionOfDefinition(args.time);
#endif
    if ( !Coords::rectIsEmpty(srcRoD) ) {
        OfxRectD srcRoI = args.regionOfInterest;
        srcRoI.x1 = srcRoD.x1;
        srcRoI.x2 = srcRoD.x2;
        rois.setRegionOfInterest(*_srcClip, srcRoI);
    }

    if ( !hasDisparity() ) {
        return;
    }
    // the disparity: the pixel of the left view at the converge point (see fetchDisparity())
    const double par = _dispClip->getPixelAspectRatio();
    OfxRectD dispRoI = {0., 0., 0., 0.};
    if ( const std::optional<OfxPointI> p = convergePixel(_convergePoint->getValueAtTime(args.time),
                                                          getDisparityBounds(args.time, args.renderScale),
                                                          args.renderScale, par) ) {
        const OfxRectI pixel = {p->x, p->y, p->x + 1, p->y + 1};
        Coords::toCanonical(pixel, args.renderScale, par, &dispRoI);
    }
    rois.setRegionOfInterest(*_dispClip, dispRoI);
}

void
ReConvergePlugin::changedClip(const InstanceChangedArgs & /*args*/,
                              const std::string &clipName)
{
    if (clipName == kClipDisparity || clipName == kOfxImageEffectSimpleSourceClipName) {
        updateConvergePointEnabled();
        if ( _disparityWarned.exchange(false) ) {
            clearPersistentMessage(); // only our 8- or 16-bit warning, not another message
        }
    }
}

#ifdef OFX_EXTENSIONS_NUKE
void
ReConvergePlugin::getFrameViewsNeeded(const FrameViewsNeededArguments& args,
                                      FrameViewsNeededSetter& frameViews)
{
    OfxRangeD range;

    range.min = range.max = args.time;
    // the source in the rendered view, the disparity in the left view
    frameViews.addFrameViewsNeeded(*_srcClip, range, args.view);
    if (_dispClip) {
        frameViews.addFrameViewsNeeded(*_dispClip, range, kLeftView);
    }
}
#endif

// the internal render function
template <int nComponents>
void
ReConvergePlugin::renderInternal(const RenderArguments &args,
                                 BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case eBitDepthUByte: {
        ImageShifter<unsigned char, nComponents> processor(*this);
        setupAndProcess(processor, args);
        break;
    }
    case eBitDepthUShort: {
        ImageShifter<unsigned short, nComponents> processor(*this);
        setupAndProcess(processor, args);
        break;
    }
    case eBitDepthFloat: {
        ImageShifter<float, nComponents> processor(*this);
        setupAndProcess(processor, args);
        break;
    }
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
ReConvergePlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    const BitDepthEnum dstBitDepth = _dstClip->getPixelDepth();
    const PixelComponentEnum dstComponents = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip->isConnected() || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip->isConnected() || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    if (dstComponents == ePixelComponentRGBA) {
        renderInternal<4>(args, dstBitDepth);
    } else if (dstComponents == ePixelComponentRGB) {
        renderInternal<3>(args, dstBitDepth);
#ifdef OFX_EXTENSIONS_NATRON
    } else if (dstComponents == ePixelComponentXY) {
        renderInternal<2>(args, dstBitDepth);
#endif
    } else {
        assert(dstComponents == ePixelComponentAlpha);
        renderInternal<1>(args, dstBitDepth);
    }
}

mDeclarePluginFactory(ReConvergePluginFactory, {ofxsThreadSuiteCheck();}, {});
struct ConvergePointParam
{
    static const char* name() { return kParamConvergePoint; }

    static const char* interactiveName() { return kParamInteractive; }
};

void
ReConvergePluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // The filter context has no Disparity input (only the offset is applied); the general context
    // adds the optional Disparity input, as the OFX spec suggests for optional inputs.
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);

    // add supported pixel depths
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);

#ifdef OFX_EXTENSIONS_NUKE
    // We only render the colour plane
    desc.setIsMultiPlanar(false);
#endif

    // We use the view calls (getFrameViewsNeeded), and the views are shifted differently
    desc.setIsViewAware(true);
    desc.setIsViewInvariant(eViewInvarianceAllViewsVariant);

    ofxsSetOverlayInteractDescriptorV2(desc, new PositionOverlayDescriptorV2<ConvergePointParam>);
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

void
ReConvergePluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                           ContextEnum context)
{
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    srcClip->addSupportedComponent(ePixelComponentXY);
#endif
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    if (context == eContextGeneral) {
        // the optional disparity: the horizontal disparity is its first channel
        ClipDescriptor *dispClip = desc.defineClip(kClipDisparity);
        dispClip->addSupportedComponent(ePixelComponentRGBA);
        dispClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
        dispClip->addSupportedComponent(ePixelComponentXY);
#endif
        dispClip->addSupportedComponent(ePixelComponentAlpha);
        dispClip->setTemporalClipAccess(false);
        dispClip->setOptional(true);
        dispClip->setSupportsTiles(kSupportsTiles);
        dispClip->setIsMask(false);
    }

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    dstClip->addSupportedComponent(ePixelComponentXY);
#endif
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // convergePoint: (0,0) by default (canonical, so not a normalised default)
    {
        Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamConvergePoint);
        param->setLabel(kParamConvergePointLabel);
        param->setHint(kParamConvergePointHint);
        param->setDoubleType(eDoubleTypeXYAbsolute);
        param->setDefault(0., 0.);
        param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(-10000, -10000, 10000, 10000); // Resolve requires display range or values are clamped to (-1,1)
        param->setIncrement(1.);
        param->setAnimates(true);
        if (context != eContextGeneral) {
            param->setIsSecretAndDisabled(true); // no Disparity input
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamInteractive);
        param->setLabel(kParamInteractiveLabel);
        param->setHint(kParamInteractiveHint);
        param->setAnimates(false);
        if (context != eContextGeneral) {
            param->setIsSecretAndDisabled(true); // no Disparity input
        }
        if (page) {
            page->addChild(*param);
        }
    }

    // offset: a double, truncated in render
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamOffset);
        param->setLabel(kParamOffsetLabel);
        param->setHint(kParamOffsetHint);
        param->setDefault(0.);
        param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(-100, 100);
        param->setIncrement(1.);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // convergeMode
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamConvergeMode);
        param->setLabel(kParamConvergeModeLabel);
        param->setHint(kParamConvergeModeHint);
        assert(param->getNOptions() == eConvergeModeShiftRight);
        param->appendOption(kParamConvergeModeOptionShiftRight);
        assert(param->getNOptions() == eConvergeModeShiftLeft);
        param->appendOption(kParamConvergeModeOptionShiftLeft);
        assert(param->getNOptions() == eConvergeModeShiftBoth);
        param->appendOption(kParamConvergeModeOptionShiftBoth);
        param->setDefault(eConvergeModeShiftRight);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
} // ReConvergePluginFactory::describeInContext

ImageEffect*
ReConvergePluginFactory::createInstance(OfxImageEffectHandle handle,
                                        ContextEnum /*context*/)
{
    return new ReConvergePlugin(handle);
}

ReConvergePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
