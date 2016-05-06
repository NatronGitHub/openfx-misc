/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2015 INRIA
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
#include <cfloat>
#include <algorithm>

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxsCoords.h"
#include "ofxsCopier.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "AdjustRoD"
#define kPluginGrouping "Transform"
#define kPluginDescription "Enlarges the input image by a given amount of black and transparent pixels."
#define kPluginIdentifier "net.sf.openfx.AdjustRoDPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamAddPixels "addPixels"
#define kParamAddPixelsLabel "Add Pixels"
#define kParamAddPixelsHint "How many pixels to add on each side for both dimensions (width/height)"


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class AdjustRoDPlugin
    : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    AdjustRoDPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(0)
        , _srcClip(0)
        , _size(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (_dstClip->getPixelComponents() == ePixelComponentAlpha ||
                             _dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == OFX::eContextGenerator) ||
                ( _srcClip && (_srcClip->getPixelComponents() == ePixelComponentAlpha ||
                               _srcClip->getPixelComponents() == ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA) ) );

        _size = fetchDouble2DParam(kParamAddPixels);
        assert(_size);
    }

private:
    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    template <int nComponents>
    void renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndCopy(OFX::PixelProcessorFilterBase &, const OFX::RenderArguments &args);

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;
    OFX::Double2DParam* _size;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
AdjustRoDPlugin::setupAndCopy(OFX::PixelProcessorFilterBase & processor,
                              const OFX::RenderArguments &args)
{
    std::auto_ptr<OFX::Image> dst( _dstClip->fetchImage(args.time) );

    if ( !dst.get() ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    std::auto_ptr<const OFX::Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                         _srcClip->fetchImage(args.time) : 0 );
    if ( src.get() && dst.get() ) {
        OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
        OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
        OFX::BitDepthEnum srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }

    // set the images
    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
void
AdjustRoDPlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args,
                                      OFX::RegionOfInterestSetter &rois)
{
    if (!_srcClip) {
        return;
    }
    const OfxRectD srcRod = _srcClip->getRegionOfDefinition(args.time);
    if ( OFX::Coords::rectIsEmpty(srcRod) || OFX::Coords::rectIsEmpty(args.regionOfInterest) ) {
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
    OFX::Coords::rectIntersection(paddedRoD, args.regionOfInterest, &paddedRoD);
    // intersect the crop rectangle with srcRoD
    OFX::Coords::rectIntersection(paddedRoD, srcRod, &paddedRoD);
    rois.setRegionOfInterest(*_srcClip, paddedRoD);
}

bool
AdjustRoDPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args,
                                       OfxRectD &rod)
{
    if (!_srcClip) {
        return false;
    }
    const OfxRectD& srcRod = _srcClip->getRegionOfDefinition(args.time);
    if ( OFX::Coords::rectIsEmpty(srcRod) ) {
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
AdjustRoDPlugin::renderInternal(const OFX::RenderArguments &args,
                                OFX::BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case OFX::eBitDepthUByte: {
        OFX::PixelCopier<unsigned char, nComponents> fred(*this);
        setupAndCopy(fred, args);
        break;
    }
    case OFX::eBitDepthUShort: {
        OFX::PixelCopier<unsigned short, nComponents> fred(*this);
        setupAndCopy(fred, args);
        break;
    }
    case OFX::eBitDepthFloat: {
        OFX::PixelCopier<float, nComponents> fred(*this);
        setupAndCopy(fred, args);
        break;
    }
    default:
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
AdjustRoDPlugin::render(const OFX::RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(dstComponents == OFX::ePixelComponentRGBA || dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentXY || dstComponents == OFX::ePixelComponentAlpha);
    if (dstComponents == OFX::ePixelComponentRGBA) {
        renderInternal<4>(args, dstBitDepth);
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        renderInternal<3>(args, dstBitDepth);
    } else if (dstComponents == OFX::ePixelComponentXY) {
        renderInternal<2>(args, dstBitDepth);
    } else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        renderInternal<1>(args, dstBitDepth);
    }
}

bool
AdjustRoDPlugin::isIdentity(const IsIdentityArguments &args,
                            Clip * &identityClip,
                            double & /*identityTime*/)
{
    double w, h;

    _size->getValueAtTime(args.time, w, h);
    if ( (w == 0) && (h == 0) ) {
        identityClip = _srcClip;

        return true;
    }

    return false;
}

mDeclarePluginFactory(AdjustRoDPluginFactory, {}, {});
void
AdjustRoDPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
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

OFX::ImageEffect*
AdjustRoDPluginFactory::createInstance(OfxImageEffectHandle handle,
                                       OFX::ContextEnum /*context*/)
{
    return new AdjustRoDPlugin(handle);
}

void
AdjustRoDPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                          OFX::ContextEnum /*context*/)
{
    // Source clip only in the filter context
    // create the mandated source clip
    // always declare the source clip first, because some hosts may consider
    // it as the default input clip (e.g. Nuke)
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentXY);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentXY);
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
        //param->setDefaultCoordinateSystem(eCoordinatesNormalised);
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
}

static AdjustRoDPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
