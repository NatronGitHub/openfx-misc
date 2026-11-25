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
 * OFX AdjustRoD plugin.
 */

#include <cmath>
#include <cfloat> // DBL_MAX
#include <algorithm>

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxsCoords.h"
#include "ofxsCopier.h"
#include "ofxsThreadSuite.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "AdjustRoD"
#define kPluginGrouping "Transform"
#define kPluginDescription "Enlarges the input image by a given amount, or shrinks it with negative amounts. The added pixels are copies of the closest edge pixel (Border Conditions: Nearest, the default), or black and transparent (Border Conditions: Black). A negative amount removes pixels on each side: the region of definition is the input's, shrunk by that amount, and the pixels outside it are those of the shrunk image under the same border conditions. The default amount is 25."
#define kPluginIdentifier "net.sf.openfx.AdjustRoDPlugin"
// History:
// 1.0 initial version
// 1.1 add boundary param
// 1.2 a NULL Source image (disconnected, or fetchImage failed) renders black instead of crashing;
//     with Nearest border conditions, ask for the Source edge pixels even where the render window
//     is out of the Source RoD (it got no Source image there);
//     (#299, in the same unreleased minor version) negative amounts shrink the region of definition (the range
//     started at 0); the pixels outside the shrunk region follow the border conditions, from its edge
// 2.0 the default amount is 25 (it was 0, a no-op); the amounts are whole pixels, truncated toward zero (in x,
//     Add Pixels divided by the pixel aspect ratio) (#299). Version 1 is kept, obsolete, for old projects.
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.
#define kPluginVersionMinorV1 2 // the minor version of the obsolete version 1
#define kParamAddPixelsDefault 25. // version 2
#define kParamAddPixelsDefaultV1 0.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamAddPixels "addPixels"
#define kParamAddPixelsLabel "Add Pixels"
#define kParamAddPixelsHintV1 "How many pixels to add on each side for both dimensions (width/height), in canonical coordinates. Negative values remove pixels: the region of definition shrinks (down to an empty region at its centre)."
#define kParamAddPixelsHint "How many pixels to add on each side for both dimensions (width/height), in canonical coordinates, truncated toward zero to whole pixels (in x, the amount is divided by the pixel aspect ratio). Negative values remove pixels: the region of definition shrinks (down to an empty region at its centre)."

#define kParamBoundary "boundary"
#define kParamBoundaryLabel "Border Conditions" //"Boundary Conditions"
#define kParamBoundaryHint "Specifies how pixel values are computed out of the image domain. This mostly affects values at the boundary of the image. If the image represents intensities, Nearest (Neumann) conditions should be used. If the image represents gradients or derivatives, Black (Dirichlet) boundary conditions should be used."
#define kParamBoundaryOptionDirichlet "Black", "Dirichlet boundary condition: pixel values out of the image domain are zero.", "black"
#define kParamBoundaryOptionNeumann "Nearest", "Neumann boundary condition: pixel values out of the image domain are those of the closest pixel location in the image domain.", "nearest"
#define kParamBoundaryOptionPeriodic "Periodic", "Image is considered to be periodic out of the image domain.", "periodic"

#ifdef OFX_EXTENSIONS_NATRON
#define OFX_COMPONENTS_OK(c) ((c)== ePixelComponentAlpha || (c) == ePixelComponentXY || (c) == ePixelComponentRGB || (c) == ePixelComponentRGBA)
#else
#define OFX_COMPONENTS_OK(c) ((c)== ePixelComponentAlpha || (c) == ePixelComponentRGB || (c) == ePixelComponentRGBA)
#endif


enum BoundaryEnum
{
    eBoundaryDirichlet = 0,
    eBoundaryNeumann,
    //eBoundaryPeriodic,
};

// The output RoD: the Source RoD enlarged by (w,h) on each side, or shrunk if they are negative. Along an axis
// shrunk by more than half its size, the RoD is empty, at the centre of the Source RoD.
static OfxRectD
adjustedRoD(const OfxRectD& srcRod,
            double w,
            double h)
{
    OfxRectD rod = srcRod;

    rod.x1 -= w;
    rod.x2 += w;
    rod.y1 -= h;
    rod.y2 += h;
    if (rod.x2 < rod.x1) {
        rod.x1 = rod.x2 = (srcRod.x1 + srcRod.x2) / 2;
    }
    if (rod.y2 < rod.y1) {
        rod.y1 = rod.y2 = (srcRod.y1 + srcRod.y2) / 2;
    }

    return rod;
}

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class AdjustRoDPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    AdjustRoDPlugin(OfxImageEffectHandle handle,
                    bool wholePixels)
        : ImageEffect(handle)
        , _wholePixels(wholePixels)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _size(NULL)
    {

        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() ||
                             _dstClip->getPixelComponents() == ePixelComponentAlpha ||
                             _dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentAlpha ||
                               _srcClip->getPixelComponents() == ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA) ) );

        _size = fetchDouble2DParam(kParamAddPixels);
        _boundary  = fetchChoiceParam(kParamBoundary);
        assert(_size && _boundary);
    }

private:
    // override the roi call
    virtual void getRegionsOfInterest(const RegionsOfInterestArguments &args, RegionOfInterestSetter &rois) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;

    template <int nComponents>
    void renderInternal(const RenderArguments &args, BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndCopy(PixelProcessorFilterBase &, const RenderArguments &args);

    // the amounts added on each side (canonical, negative to shrink)
    void getAmounts(double time, double* w, double* h) const;

private:
    const bool _wholePixels; // version 2: the amounts are whole pixels, truncated toward zero
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    Double2DParam* _size;
    ChoiceParam* _boundary;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
AdjustRoDPlugin::setupAndCopy(PixelProcessorFilterBase & processor,
                              const RenderArguments &args)
{
    const double time = args.time;
    auto_ptr<Image> dst( _dstClip->fetchImage(time) );

    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    checkBadRenderScaleOrField(dst, args);
    auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                    _srcClip->fetchImage(args.time) : 0 );
    if ( src.get() && dst.get() ) {
        BitDepthEnum dstBitDepth       = dst->getPixelDepth();
        PixelComponentEnum dstComponents  = dst->getPixelComponents();
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatFailed);
        }
    }

    if ( !src.get() ) {
        // no Source image (disconnected, or the host could not fetch it, e.g. because the
        // render window is out of the Source RoD): black and transparent
        fillBlack( *this, args.renderWindow, args.renderScale, dst.get() );

        return;
    }

    // set the images
    processor.setDstImg( dst.get() );
    const int boundary = _boundary->getValueAtTime(time);
    double w = 0., h = 0.;
    getAmounts(time, &w, &h);
    if ( (w < 0) || (h < 0) ) {
        // shrunk: only the Source pixels in the output RoD are shown, and the border conditions apply from
        // its edge. The host may give a larger Source image: use the part of it in the shrunk RoD.
        const OfxRectD shownRod = adjustedRoD(_srcClip->getRegionOfDefinition(time), (std::min)(w, 0.), (std::min)(h, 0.));
        OfxRectI shownPixel;
        Coords::toPixelEnclosing(shownRod, args.renderScale, src->getPixelAspectRatio(), &shownPixel);
        OfxRectI bounds;
        if ( Coords::rectIsEmpty(shownRod) || !Coords::rectIntersection(shownPixel, src->getBounds(), &bounds) || Coords::rectIsEmpty(bounds) ) {
            fillBlack( *this, args.renderWindow, args.renderScale, dst.get() );

            return;
        }
        processor.setSrcImg( src->getPixelAddress(bounds.x1, bounds.y1), bounds, src->getPixelComponents(),
                             src->getPixelComponentCount(), src->getPixelDepth(), src->getRowBytes(), boundary );
    } else {
        processor.setSrcImg( src.get(), boundary );
    }

    // set the render window
    processor.setRenderWindow(args.renderWindow, args.renderScale);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

// Version 2: whole pixels at render scale 1, truncated toward zero: Add Pixels is in canonical coordinates, so
// the x amount is divided by the pixel aspect ratio (as a float: e.g. 16 at PAR 768/720 gives 14 pixels, not 15).
void
AdjustRoDPlugin::getAmounts(double time,
                            double* w,
                            double* h) const
{
    _size->getValueAtTime(time, *w, *h);
    if (!_wholePixels) {
        return;
    }
    const double par = ( _srcClip && _srcClip->isConnected() ) ? _srcClip->getPixelAspectRatio() : 1.;
    if (par > 0.) {
        const double parf = static_cast<float>(par);
        *w = std::trunc(*w / parf) * par;
    }
    *h = std::trunc(*h);
}

// Along one axis: the part of the Source range [s1,s2) that Nearest border conditions need to
// render the range [r1,r2): [r1,r2) clamped to [s1,s2), and at least the edge pixel of the Source
// (of size pixelSize) on the side where [r1,r2) is out of [s1,s2).
static void
nearestRoI(double r1,
           double r2,
           double s1,
           double s2,
           double pixelSize,
           double* o1,
           double* o2)
{
    double a = (std::max)( s1, (std::min)(r1, s2) );
    double b = (std::max)( s1, (std::min)(r2, s2) );

    if (b <= a) {
        if (r1 >= s2) {
            a = (std::max)(s1, s2 - pixelSize);
            b = s2;
        } else {
            a = s1;
            b = (std::min)(s2, s1 + pixelSize);
        }
    }
    *o1 = a;
    *o2 = b;
}

// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
void
AdjustRoDPlugin::getRegionsOfInterest(const RegionsOfInterestArguments &args,
                                      RegionOfInterestSetter &rois)
{
    if (!_srcClip || !_srcClip->isConnected()) {
        return;
    }
    const OfxRectD srcRod = _srcClip->getRegionOfDefinition(args.time);
    if ( Coords::rectIsEmpty(srcRod) || Coords::rectIsEmpty(args.regionOfInterest) ) {
        return;
    }
    double w = 0., h = 0.;
    getAmounts(args.time, &w, &h);

    OfxRectD paddedRoD = adjustedRoD(srcRod, w, h);
    // the part of the Source that the output shows: all of it, or the shrunk RoD
    const OfxRectD shownRod = (w < 0 || h < 0) ? adjustedRoD(srcRod, (std::min)(w, 0.), (std::min)(h, 0.)) : srcRod;
    if ( Coords::rectIsEmpty(shownRod) ) {
        // the output RoD is empty (render fills black): keep the default RoI

        return;
    }

    if (_boundary->getValueAtTime(args.time) == eBoundaryNeumann) {
        // Out of the shown Source RoD, the output pixels are those of the nearest shown Source pixel: they
        // need its edge pixels even where args.regionOfInterest is out of it.
        const double par = _srcClip->getPixelAspectRatio();
        // the size of a pixel in canonical coordinates (renderScale and the PAR are > 0 per the spec)
        const double pixelW = (args.renderScale.x > 0. && par > 0.) ? par / args.renderScale.x : 1.;
        const double pixelH = (args.renderScale.y > 0.) ? 1. / args.renderScale.y : 1.;
        OfxRectD roi;
        nearestRoI(args.regionOfInterest.x1, args.regionOfInterest.x2, shownRod.x1, shownRod.x2, pixelW, &roi.x1, &roi.x2);
        nearestRoI(args.regionOfInterest.y1, args.regionOfInterest.y2, shownRod.y1, shownRod.y2, pixelH, &roi.y1, &roi.y2);
        rois.setRegionOfInterest(*_srcClip, roi);

        return;
    }

    // intersect the crop rectangle with args.regionOfInterest
    Coords::rectIntersection(paddedRoD, args.regionOfInterest, &paddedRoD);
    // intersect the crop rectangle with the shown part of srcRoD
    Coords::rectIntersection(paddedRoD, shownRod, &paddedRoD);
    rois.setRegionOfInterest(*_srcClip, paddedRoD);
}

bool
AdjustRoDPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                       OfxRectD &rod)
{
    if (!_srcClip || !_srcClip->isConnected()) {
        return false;
    }
    const OfxRectD& srcRod = _srcClip->getRegionOfDefinition(args.time);
    if ( Coords::rectIsEmpty(srcRod) ) {
        return false;
    }
    double w = 0., h = 0.;
    getAmounts(args.time, &w, &h);

    rod = adjustedRoD(srcRod, w, h);

    return true;
}

// the internal render function
template <int nComponents>
void
AdjustRoDPlugin::renderInternal(const RenderArguments &args,
                                BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case eBitDepthUByte: {
        PixelCopier<unsigned char, nComponents> fred(*this);
        setupAndCopy(fred, args);
        break;
    }
    case eBitDepthUShort: {
        PixelCopier<unsigned short, nComponents> fred(*this);
        setupAndCopy(fred, args);
        break;
    }
    case eBitDepthFloat: {
        PixelCopier<float, nComponents> fred(*this);
        setupAndCopy(fred, args);
        break;
    }
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
AdjustRoDPlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(OFX_COMPONENTS_OK(dstComponents));
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

bool
AdjustRoDPlugin::isIdentity(const IsIdentityArguments &args,
                            Clip * &identityClip,
                            double & /*identityTime*/
                            , int& /*view*/, std::string& /*plane*/)
{
    double w = 0., h = 0.;

    getAmounts(args.time, &w, &h);
    if ( (w == 0) && (h == 0) ) {
        identityClip = _srcClip;

        return true;
    }

    return false;
}

mDeclarePluginFactoryVersioned(AdjustRoDPluginFactory, {ofxsThreadSuiteCheck();}, {});
template<unsigned int majorVersion>
void
AdjustRoDPluginFactory<majorVersion>::describe(ImageEffectDescriptor &desc)
{
    // version 1 (Natron 2.5 and earlier) is kept for old projects
    desc.setIsObsolete(majorVersion < kPluginVersionMajor);
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextFilter);

    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);


    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(true);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);

    desc.setSupportsTiles(kSupportsTiles);

    // in order to support multiresolution, render() must take into account the pixelaspectratio and the renderscale
    // and scale the transform appropriately.
    // All other functions are usually in canonical coordinates.
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
#ifdef OFX_EXTENSIONS_NUKE
    // ask the host to render all planes
    desc.setPassThroughForNotProcessedPlanes(ePassThroughLevelRenderAllRequestedPlanes);
#endif
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

template<unsigned int majorVersion>
ImageEffect*
AdjustRoDPluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                       ContextEnum /*context*/)
{
    return new AdjustRoDPlugin(handle, /*wholePixels=*/ majorVersion >= 2);
}

template<unsigned int majorVersion>
void
AdjustRoDPluginFactory<majorVersion>::describeInContext(ImageEffectDescriptor &desc,
                                          ContextEnum /*context*/)
{
    // Source clip only in the filter context
    // create the mandated source clip
    // always declare the source clip first, because some hosts may consider
    // it as the default input clip (e.g. Nuke)
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


    // size
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamAddPixels);
        param->setLabel(kParamAddPixelsLabel);
        param->setHint( (majorVersion >= 2) ? kParamAddPixelsHint : kParamAddPixelsHintV1 );
        param->setDoubleType(eDoubleTypeXY);
        param->setDefaultCoordinateSystem(eCoordinatesCanonical); // Nuke defaults to Normalized for XY and XYAbsolute!
        const double amount = (majorVersion >= 2) ? kParamAddPixelsDefault : kParamAddPixelsDefaultV1;
        param->setDefault(amount, amount);
        param->setIncrement(1.);
        param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX); // negative values shrink the RoD (#299)
        param->setDisplayRange(-1000., -1000., 1000., 1000.);
        param->setDimensionLabels("w", "h");
        param->setIncrement(1.);
        param->setDigits(0);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamBoundary);
        param->setLabel(kParamBoundaryLabel);
        param->setHint(kParamBoundaryHint);
        assert(param->getNOptions() == eBoundaryDirichlet && param->getNOptions() == 0);
        param->appendOption(kParamBoundaryOptionDirichlet);
        assert(param->getNOptions() == eBoundaryNeumann && param->getNOptions() == 1);
        param->appendOption(kParamBoundaryOptionNeumann);
        //assert(param->getNOptions() == eBoundaryPeriodic && param->getNOptions() == 2);
        //param->appendOption(kParamBoundaryOptionPeriodic);
        param->setDefault( (int)eBoundaryNeumann ); // Nearest: edge replication
        if (page) {
            page->addChild(*param);
        }
    }
} // AdjustRoDPluginFactory::describeInContext

static AdjustRoDPluginFactory<kPluginVersionMajor> p(kPluginIdentifier, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)
static AdjustRoDPluginFactory<1> p_v1(kPluginIdentifier, kPluginVersionMinorV1);
mRegisterPluginFactoryInstance(p_v1)

OFXS_NAMESPACE_ANONYMOUS_EXIT
