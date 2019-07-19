/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2018 INRIA
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
#define kPluginDescription "Enlarges the input image by a given amount of black and transparent pixels."
#define kPluginIdentifier "net.sf.openfx.AdjustRoDPlugin"
// History:
// 1.0 initial version
// 1.1 add boundary param
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 1 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamAddPixels "addPixels"
#define kParamAddPixelsLabel "Add Pixels"
#define kParamAddPixelsHint "How many pixels to add on each side for both dimensions (width/height)"

#define kParamBoundary "boundary"
#define kParamBoundaryLabel "Border Conditions" //"Boundary Conditions"
#define kParamBoundaryHint "Specifies how pixel values are computed out of the image domain. This mostly affects values at the boundary of the image. If the image represents intensities, Nearest (Neumann) conditions should be used. If the image represents gradients or derivatives, Black (Dirichlet) boundary conditions should be used."
#define kParamBoundaryOptionDirichlet "Black", "Dirichlet boundary condition: pixel values out of the image domain are zero.", "black"
#define kParamBoundaryOptionNeumann "Nearest", "Neumann boundary condition: pixel values out of the image domain are those of the closest pixel location in the image domain.", "nearest"
#define kParamBoundaryOptionPeriodic "Periodic", "Image is considered to be periodic out of the image domain.", "periodic"
#define kParamBoundaryDefault eBoundaryDirichlet
#define kParamBoundaryDefaultLaplacian eBoundaryNeumann
#define kParamBoundaryDefaultBloom eBoundaryNeumann
#define kParamBoundaryDefaultEdgeExtend eBoundaryNeumann

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

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class AdjustRoDPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    AdjustRoDPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
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

private:
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
# ifndef NDEBUG
    if ( src.get() && dst.get() ) {
        BitDepthEnum dstBitDepth       = dst->getPixelDepth();
        PixelComponentEnum dstComponents  = dst->getPixelComponents();
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatFailed);
        }
    }
# endif

    // set the images
    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get(), _boundary->getValueAtTime(time) );

    // set the render window
    processor.setRenderWindow(args.renderWindow, args.renderScale);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
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
    double w, h;
    _size->getValueAtTime(args.time, w, h);

    OfxRectD paddedRoD = srcRod;
    paddedRoD.x1 -= w;
    paddedRoD.x2 += w;
    paddedRoD.y1 -= h;
    paddedRoD.y2 += h;

    // intersect the crop rectangle with args.regionOfInterest
    Coords::rectIntersection(paddedRoD, args.regionOfInterest, &paddedRoD);
    // intersect the crop rectangle with srcRoD
    Coords::rectIntersection(paddedRoD, srcRod, &paddedRoD);
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
    double w, h;
    _size->getValueAtTime(args.time, w, h);

    rod = srcRod;
    rod.x1 -= w;
    rod.x2 += w;
    rod.y1 -= h;
    rod.y2 += h;

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
    double w, h;

    _size->getValueAtTime(args.time, w, h);
    if ( (w == 0) && (h == 0) ) {
        identityClip = _srcClip;

        return true;
    }

    return false;
}

mDeclarePluginFactory(AdjustRoDPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
AdjustRoDPluginFactory::describe(ImageEffectDescriptor &desc)
{
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

ImageEffect*
AdjustRoDPluginFactory::createInstance(OfxImageEffectHandle handle,
                                       ContextEnum /*context*/)
{
    return new AdjustRoDPlugin(handle);
}

void
AdjustRoDPluginFactory::describeInContext(ImageEffectDescriptor &desc,
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
        param->setHint(kParamAddPixelsHint);
        param->setDoubleType(eDoubleTypeXY);
        param->setDefaultCoordinateSystem(eCoordinatesCanonical); // Nuke defaults to Normalized for XY and XYAbsolute!
        param->setDefault(0., 0.);
        param->setIncrement(1.);
        param->setRange(0., 0., DBL_MAX, DBL_MAX);
        param->setDisplayRange(0., 0., 1000., 1000.);
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
        param->setDefault( (int)eBoundaryNeumann ); // aka zero
        if (page) {
            page->addChild(*param);
        }
    }
} // AdjustRoDPluginFactory::describeInContext

static AdjustRoDPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
