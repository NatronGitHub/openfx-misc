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
 * OFX Crop plugin.
 */

#include "Crop.h"

#include <cmath>
#include <algorithm>

#include "ofxsProcessing.H"
#include "ofxsCoords.h"
#include "ofxsRectangleInteract.h"
#include "ofxsMacros.h"

#define kPluginName "CropOFX"
#define kPluginGrouping "Transform"
#define kPluginDescription "Removes everything outside the defined rectangle and adds black edges so everything outside is black.\n"\
"This plugin does not concatenate transforms."
#define kPluginIdentifier "net.sf.openfx.CropPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamReformat "reformat"
#define kParamReformatLabel "Reformat"
#define kParamReformatHint "Translates the bottom left corner of the crop rectangle to be in (0,0)."

#define kParamIntersect "intersect"
#define kParamIntersectLabel "Intersect"
#define kParamIntersectHint "Intersects the crop rectangle with the input region of definition instead of extending it."

#define kParamBlackOutside "blackOutside"
#define kParamBlackOutsideLabel "Black Outside"
#define kParamBlackOutsideHint "Add 1 black and transparent pixel to the region of definition so that all the area outside the crop rectangle is black."

#define kParamSoftness "softness"
#define kParamSoftnessLabel "Softness"
#define kParamSoftnessHint "Size of the fade to black around edges to apply."

static inline
double
rampSmooth(double t)
{
    t *= 2.;
    if (t < 1) {
        return t * t / (2.);
    } else {
        t -= 1.;
        return -0.5 * (t * (t - 2) - 1);
    }
}

using namespace OFX;

class CropProcessorBase : public OFX::ImageProcessor
{
   
    
protected:
    const OFX::Image *_srcImg;
    
    OfxPointD _btmLeft, _size;
    double _softness;
    bool _blackOutside;
    OfxPointI _translation;
    OfxRectI _dstRoDPix;
    
public:
    CropProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _softness(0)
    , _blackOutside(false)
    {
        _btmLeft.x = _btmLeft.y = 0.;
        _size.x = _size.y = 0.;
        _translation.x = _translation.y = 0;
        _dstRoDPix.x1 = _dstRoDPix.y1 = _dstRoDPix.x2 = _dstRoDPix.y2 = 0;
    }

    /** @brief set the src image */
    void setSrcImg(const OFX::Image *v)
    {
        _srcImg = v;
    }

    
    void setValues(const OfxPointD& btmLeft,
                   const OfxPointD& size,
                   const OfxRectI& cropRect,
                   const OfxRectI& dstRoDPix,
                   bool blackOutside,
                   bool reformat,
                   double softness)
    {
        _btmLeft = btmLeft;
        _size = size;
        _softness = softness;
        _blackOutside = blackOutside;
        _dstRoDPix = dstRoDPix;
        if (reformat) {
            _translation.x = cropRect.x1;
            _translation.y = cropRect.y1;
        } else {
            _translation.x = 0;
            _translation.y = 0;
        }
    }

};


template <class PIX, int nComponents, int maxValue>
class CropProcessor : public CropProcessorBase
{
public:
    CropProcessor(OFX::ImageEffect &instance)
    : CropProcessorBase(instance)
    {
    }

private:
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        
        //assert(filter == _filter);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if (_effect.abort()) {
                break;
            }
            
            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            
            bool yblack = _blackOutside && (y == _dstRoDPix.y1 || y == (_dstRoDPix.y2 - 1));

            for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                bool xblack = _blackOutside && (x == _dstRoDPix.x1 || x == (_dstRoDPix.x2 - 1));
                // treat the black case separately
                if (xblack || yblack || !_srcImg) {
                    for (int k = 0; k < nComponents; ++k) {
                        dstPix[k] =  PIX();
                    }
                } else {
                    OfxPointI p_pixel;
                    OfxPointD p;
                    p_pixel.x = x + _translation.x;
                    p_pixel.y = y + _translation.y;
                    OFX::Coords::toCanonical(p_pixel, _dstImg->getRenderScale(), _dstImg->getPixelAspectRatio(), &p);

                    double dx = std::min(p.x - _btmLeft.x, _btmLeft.x + _size.x - p.x);
                    double dy = std::min(p.y - _btmLeft.y, _btmLeft.y + _size.y - p.y);

                    if (dx <=0 || dy <= 0) {
                        // outside of the rectangle
                        for (int k = 0; k < nComponents; ++k) {
                            dstPix[k] =  PIX();
                        }
                    } else {
                        const PIX *srcPix = (const PIX*)_srcImg->getPixelAddress(p_pixel.x, p_pixel.y);
                        if (!srcPix) {
                            for (int k = 0; k < nComponents; ++k) {
                                dstPix[k] =  PIX();
                            }
                        } else if (_softness == 0 || (dx >= _softness && dy >= _softness)) {
                            // inside of the rectangle
                            for (int k = 0; k < nComponents; ++k) {
                                dstPix[k] =  srcPix[k];
                            }
                        } else {
                            double tx, ty;
                            if (dx >= _softness) {
                                tx = 1.;
                            } else {
                                tx = rampSmooth(dx / _softness);
                            }
                            if (dy >= _softness) {
                                ty = 1.;
                            } else {
                                ty = rampSmooth(dy / _softness);
                            }
                            double t = tx * ty;
                            if (t >= 1) {
                                for (int k = 0; k < nComponents; ++k) {
                                    dstPix[k] =  srcPix[k];
                                }
                            } else {
                                //if (_plinear) {
                                //    // it seems to be the way Nuke does it... I could understand t*t, but why t*t*t?
                                //    t = t*t*t;
                                //}
                                for (int k = 0; k < nComponents; ++k) {
                                    dstPix[k] =  PIX(srcPix[k] * t);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
};





////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class CropPlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    CropPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    , _btmLeft(0)
    , _size(0)
    , _softness(0)
    , _reformat(0)
    , _intersect(0)
    , _blackOutside(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == ePixelComponentAlpha ||
                            _dstClip->getPixelComponents() == ePixelComponentRGB ||
                            _dstClip->getPixelComponents() == ePixelComponentRGBA));
        _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert((!_srcClip && getContext() == OFX::eContextGenerator) ||
               (_srcClip && (_srcClip->getPixelComponents() == ePixelComponentAlpha ||
                             _srcClip->getPixelComponents() == ePixelComponentRGB ||
                             _srcClip->getPixelComponents() == ePixelComponentRGBA)));
        
        _btmLeft = fetchDouble2DParam(kParamRectangleInteractBtmLeft);
        _size = fetchDouble2DParam(kParamRectangleInteractSize);
        _softness = fetchDoubleParam(kParamSoftness);
        _reformat = fetchBooleanParam(kParamReformat);
        _intersect = fetchBooleanParam(kParamIntersect);
        _blackOutside = fetchBooleanParam(kParamBlackOutside);
        
        assert(_btmLeft && _size && _softness && _reformat && _intersect && _blackOutside);
    }
    
private:
    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;
    
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;
    
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;
    
    template <int nComponents>
    void renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(CropProcessorBase &, const OFX::RenderArguments &args);
    
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;
    
    void getCropRectangle_canonical(OfxTime time,bool useReformat,bool forceIntersect,OfxRectD& cropRect) const;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;

    OFX::Double2DParam* _btmLeft;
    OFX::Double2DParam* _size;
    OFX::DoubleParam* _softness;
    OFX::BooleanParam* _reformat;
    OFX::BooleanParam* _intersect;
    OFX::BooleanParam* _blackOutside;
};

void
CropPlugin::getCropRectangle_canonical(OfxTime time,bool useReformat,bool forceIntersect,OfxRectD& cropRect) const
{
    
    bool intersect;
    if (!forceIntersect) {
        _intersect->getValueAtTime(time, intersect);
    } else {
        intersect = true;
    }
    
    bool reformat;
    if (useReformat) {
        _reformat->getValueAtTime(time, reformat);
    } else {
        reformat = false;
    }
    
    bool blackOutside;
    _blackOutside->getValueAtTime(time, blackOutside);
    
    if (reformat) {
        cropRect.x1 = cropRect.y1 = 0.;
    } else {
        _btmLeft->getValueAtTime(time, cropRect.x1, cropRect.y1);
    }
    
    double w,h;
    _size->getValueAtTime(time, w, h);
    cropRect.x2 = cropRect.x1 + w;
    cropRect.y2 = cropRect.y1 + h;
    
    if (blackOutside) {
        cropRect.x1 -= 1;
        cropRect.y1 -= 1;
        cropRect.x2 += 1;
        cropRect.y2 += 1;
    }
    
    if (intersect && _srcClip) {
        const OfxRectD& srcRoD = _srcClip->getRegionOfDefinition(time);
        OFX::Coords::rectIntersection(cropRect, srcRoD, &cropRect);
    }
    

}

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
CropPlugin::setupAndProcess(CropProcessorBase &processor, const OFX::RenderArguments &args)
{
    std::auto_ptr<OFX::Image> dst(_dstClip->fetchImage(args.time));
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum         dstBitDepth    = dst->getPixelDepth();
    OFX::PixelComponentEnum   dstComponents  = dst->getPixelComponents();
    if (dstBitDepth != _dstClip->getPixelDepth() ||
        dstComponents != _dstClip->getPixelComponents()) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dst->getRenderScale().x != args.renderScale.x ||
        dst->getRenderScale().y != args.renderScale.y ||
        (dst->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && dst->getField() != args.fieldToRender)) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    std::auto_ptr<const OFX::Image> src((_srcClip && _srcClip->isConnected()) ?
                                        _srcClip->fetchImage(args.time) : 0);
    if (src.get()) {
        if (src->getRenderScale().x != args.renderScale.x ||
            src->getRenderScale().y != args.renderScale.y ||
            (src->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && src->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
        OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            OFX::throwSuiteStatusException(kOfxStatFailed);
        
        
    }
    
    // set the images
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());
    
    // set the render window
    processor.setRenderWindow(args.renderWindow);
    
    OfxPointD btmLeft, size;
    _btmLeft->getValueAtTime(args.time, btmLeft.x, btmLeft.y);
    _size->getValueAtTime(args.time, size.x, size.y);

    bool reformat;
    _reformat->getValueAtTime(args.time, reformat);
    bool blackOutside;
    _blackOutside->getValueAtTime(args.time, blackOutside);
    
    OfxRectD cropRectCanonical;
    getCropRectangle_canonical(args.time, false, false, cropRectCanonical);
    OfxRectI cropRectPixel;
    double par = dst->getPixelAspectRatio();
    Coords::toPixelEnclosing(cropRectCanonical, args.renderScale, par, &cropRectPixel);
    
    double softness;
    _softness->getValueAtTime(args.time, softness);
    softness *= args.renderScale.x;
    
    const OfxRectD& dstRoD = _dstClip->getRegionOfDefinition(args.time);
    OfxRectI dstRoDPix;
    Coords::toPixelEnclosing(dstRoD, args.renderScale, par, &dstRoDPix);

    processor.setValues(btmLeft, size, cropRectPixel, dstRoDPix, blackOutside, reformat, softness);
    
    // Call the base class process member, this will call the derived templated process code
    processor.process();
}



// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
void
CropPlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois)
{
    bool reformat;
    _reformat->getValueAtTime(args.time, reformat);

    OfxRectD cropRect;
    getCropRectangle_canonical(args.time, false, true, cropRect);

    OfxRectD roi = args.regionOfInterest;

    if (reformat) {
        // translate, because cropRect will be rendered at (0,0) in this case
        // Remember: this is the region of INTEREST: the region from the input
        // used to render the region args.regionOfInterest
        roi.x1 += cropRect.x1;
        roi.y1 += cropRect.y1;
        roi.x2 += cropRect.x2;
        roi.y2 += cropRect.y2;
    }

    // intersect the crop rectangle with args.regionOfInterest
    OFX::Coords::rectIntersection(cropRect, roi, &cropRect);
    rois.setRegionOfInterest(*_srcClip, cropRect);
}


bool
CropPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    getCropRectangle_canonical(args.time, true, false, rod);
    return true;
}

// the internal render function
template <int nComponents>
void
CropPlugin::renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth)
    {
        case OFX::eBitDepthUByte: {
            CropProcessor<unsigned char, nComponents, 255> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case OFX::eBitDepthUShort: {
            CropProcessor<unsigned short, nComponents, 65535> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case OFX::eBitDepthFloat: {
            CropProcessor<float, nComponents, 1> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        default:
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
CropPlugin::render(const OFX::RenderArguments &args)
{
    
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert(kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth());
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


void
CropPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kParamReformat) {
        bool reformat;
        _reformat->getValueAtTime(args.time, reformat);
        _btmLeft->setEnabled(!reformat);
    }
}


using namespace OFX;

class CropInteract : public RectangleInteract
{
public:
    
    CropInteract(OfxInteractHandle handle, OFX::ImageEffect* effect)
    : RectangleInteract(handle,effect)
    , _reformat(0)
    , _isReformated(false)
    {
        _reformat = effect->fetchBooleanParam(kParamReformat);
        addParamToSlaveTo(_reformat);
        assert(_reformat);
    }

    
private:
    
    virtual OfxPointD getBtmLeft(OfxTime time) const OVERRIDE FINAL {
        OfxPointD btmLeft;
        bool reformat;
        _reformat->getValueAtTime(time, reformat);
        if (!reformat) {
            btmLeft = RectangleInteract::getBtmLeft(time);
        } else {
            btmLeft.x = btmLeft.y = 0.;
        }
        return btmLeft;
    }
    
    virtual void aboutToCheckInteractivity(OfxTime time) OVERRIDE FINAL {
        _reformat->getValueAtTime(time,_isReformated);
    }
    
    virtual bool allowTopLeftInteraction() const OVERRIDE FINAL { return !_isReformated; }
    virtual bool allowBtmRightInteraction() const OVERRIDE FINAL { return !_isReformated; }
    virtual bool allowBtmLeftInteraction() const OVERRIDE FINAL { return !_isReformated; }
    virtual bool allowBtmMidInteraction() const OVERRIDE FINAL { return !_isReformated; }
    virtual bool allowMidLeftInteraction() const OVERRIDE FINAL { return !_isReformated; }
    virtual bool allowCenterInteraction() const OVERRIDE FINAL { return !_isReformated; }

private:
    OFX::BooleanParam* _reformat;
    bool _isReformated; //< @see aboutToCheckInteractivity
};

class CropOverlayDescriptor : public DefaultEffectOverlayDescriptor<CropOverlayDescriptor, CropInteract> {};



mDeclarePluginFactory(CropPluginFactory, {}, {});

void CropPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
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
    desc.setOverlayInteractDescriptor(new CropOverlayDescriptor);
#ifdef OFX_EXTENSIONS_NUKE
    // ask the host to render all planes
    desc.setPassThroughForNotProcessedPlanes(ePassThroughLevelRenderAllRequestedPlanes);
#endif
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}



OFX::ImageEffect* CropPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new CropPlugin(handle);
}




void CropPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/)
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

    // btmLeft
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamRectangleInteractBtmLeft);
        param->setLabel(kParamRectangleInteractBtmLeftLabel);
        param->setDoubleType(OFX::eDoubleTypeXYAbsolute);
        param->setDefaultCoordinateSystem(OFX::eCoordinatesNormalised);
        param->setDefault(0., 0.);
        param->setDisplayRange(-10000, -10000, 10000, 10000); // Resolve requires display range or values are clamped to (-1,1)
        param->setIncrement(1.);
        param->setHint("Coordinates of the bottom left corner of the crop rectangle.");
        param->setDigits(0);
        if (page) {
            page->addChild(*param);
        }
    }

    // size
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamRectangleInteractSize);
        param->setLabel(kParamRectangleInteractSizeLabel);
        param->setDoubleType(OFX::eDoubleTypeXY);
        param->setDefaultCoordinateSystem(OFX::eCoordinatesNormalised);
        param->setDefault(1., 1.);
        param->setDisplayRange(0, 0, 10000, 10000); // Resolve requires display range or values are clamped to (-1,1)
        param->setIncrement(1.);
        param->setDimensionLabels(kParamRectangleInteractSizeDim1, kParamRectangleInteractSizeDim2);
        param->setHint("Width and height of the crop rectangle.");
        param->setIncrement(1.);
        param->setDigits(0);
        if (page) {
            page->addChild(*param);
        }
    }

    // interactive
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamRectangleInteractInteractive);
        param->setLabel(kParamRectangleInteractInteractiveLabel);
        param->setHint(kParamRectangleInteractInteractiveHint);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }

    // softness
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSoftness);
        param->setLabel(kParamSoftnessLabel);
        param->setDefault(0);
        param->setRange(0., 1000.);
        param->setDisplayRange(0., 100.);
        param->setIncrement(1.);
        param->setHint(kParamSoftnessHint);
        if (page) {
            page->addChild(*param);
        }
    }

    // reformat
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamReformat);
        param->setLabel(kParamReformatLabel);
        param->setHint(kParamReformatHint);
        param->setDefault(false);
        param->setAnimates(true);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine);
        if (page) {
            page->addChild(*param);
        }
    }

    // intersect
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamIntersect);
        param->setLabel(kParamIntersectLabel);
        param->setHint(kParamIntersectHint);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine);
        param->setDefault(false);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // blackOutside
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamBlackOutside);
        param->setLabel(kParamBlackOutsideLabel);
        param->setDefault(false);
        param->setAnimates(true);
        param->setHint(kParamBlackOutsideHint);
        if (page) {
            page->addChild(*param);
        }
    }
}

void getCropPluginID(OFX::PluginFactoryArray &ids)
{
    static CropPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

