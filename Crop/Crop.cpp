/*
 OFX Crop plugin.
 
 Copyright (C) 2014 INRIA
 
 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:
 
 Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.
 
 Redistributions in binary form must reproduce the above copyright notice, this
 list of conditions and the following disclaimer in the documentation and/or
 other materials provided with the distribution.
 
 Neither the name of the {organization} nor the names of its
 contributors may be used to endorse or promote products derived from
 this software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 
 INRIA
 Domaine de Voluceau
 Rocquencourt - B.P. 105
 78153 Le Chesnay Cedex - France
 
 
 The skeleton for this source file is from:
 OFX Basic Example plugin, a plugin that illustrates the use of the OFX Support library.
 
 Copyright (C) 2004-2005 The Open Effects Association Ltd
 Author Bruno Nicoletti bruno@thefoundry.co.uk
 
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
 
 * Redistributions of source code must retain the above copyright notice,
 this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.
 * Neither the name The Open Effects Association Ltd, nor the names of its
 contributors may be used to endorse or promote products derived from this
 software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 
 The Open Effects Association Ltd
 1 Wardour St
 London W1D 6PA
 England
 
 */
#include "Crop.h"

#include <cmath>
#include <algorithm>

#include "ofxsProcessing.H"
#include "ofxsMerging.h"
#include "ofxsRectangleInteract.h"
#include "ofxsMacros.h"

#define kPluginName "CropOFX"
#define kPluginGrouping "Transform"
#define kPluginDescription "Removes everything outside the defined rectangle and adds black edges so everything outside is black."
#define kPluginIdentifier "net.sf.openfx.CropPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe

#define kParamReformat "reformat"
#define kParamReformatLabel "Reformat"
#define kParamIntersect "intersect"
#define kParamIntersectLabel "Intersect"
#define kParamBlackOutside "blackOutside"
#define kParamBlackOutsideLabel "Black Outside"
#define kParamSoftness "softness"
#define kParamSoftnessLabel "Softness"

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
                        dstPix[k] =  0.;
                    }
                } else {
                    OfxPointI p_pixel;
                    OfxPointD p;
                    p_pixel.x = x + _translation.x;
                    p_pixel.y = y + _translation.y;
                    OFX::MergeImages2D::toCanonical(p_pixel, _dstImg->getRenderScale(), _dstImg->getPixelAspectRatio(), &p);

                    double dx = std::min(p.x - _btmLeft.x, _btmLeft.x + _size.x - p.x);
                    double dy = std::min(p.y - _btmLeft.y, _btmLeft.y + _size.y - p.y);

                    if (dx <=0 || dy <= 0) {
                        // outside of the rectangle
                        for (int k = 0; k < nComponents; ++k) {
                            dstPix[k] =  0.;
                        }
                    } else {
                        const PIX *srcPix = (const PIX*)_srcImg->getPixelAddress(p_pixel.x, p_pixel.y);
                        if (!srcPix) {
                            for (int k = 0; k < nComponents; ++k) {
                                dstPix[k] =  0.;
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
                                    dstPix[k] =  srcPix[k] * t;
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
    , dstClip_(0)
    , srcClip_(0)
    , _btmLeft(0)
    , _size(0)
    , _softness(0)
    , _reformat(0)
    , _intersect(0)
    , _blackOutside(0)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        assert(dstClip_ && (dstClip_->getPixelComponents() == ePixelComponentAlpha || dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA));
        srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(srcClip_ && (srcClip_->getPixelComponents() == ePixelComponentAlpha || srcClip_->getPixelComponents() == ePixelComponentRGB || srcClip_->getPixelComponents() == ePixelComponentRGBA));
        
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
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;

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
    
    if (intersect) {
        const OfxRectD& srcRoD = srcClip_->getRegionOfDefinition(time);
        MergeImages2D::rectIntersection(cropRect, srcRoD, &cropRect);
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
    std::auto_ptr<OFX::Image> dst(dstClip_->fetchImage(args.time));
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dst->getRenderScale().x != args.renderScale.x ||
        dst->getRenderScale().y != args.renderScale.y ||
        dst->getField() != args.fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    std::auto_ptr<const OFX::Image> src(srcClip_->fetchImage(args.time));
    if (src.get() && dst.get())
    {
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
    MergeImages2D::toPixelEnclosing(cropRectCanonical, args.renderScale, par, &cropRectPixel);
    
    double softness;
    _softness->getValueAtTime(args.time, softness);
    softness *= args.renderScale.x;
    
    const OfxRectD& dstRoD = dstClip_->getRegionOfDefinition(args.time);
    OfxRectI dstRoDPix;
    MergeImages2D::toPixelEnclosing(dstRoD, args.renderScale, par, &dstRoDPix);

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
    MergeImages2D::rectIntersection(cropRect, roi, &cropRect);
    rois.setRegionOfInterest(*srcClip_, cropRect);
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
        case OFX::eBitDepthUByte :
        {
            CropProcessor<unsigned char, nComponents, 255> fred(*this);
            setupAndProcess(fred, args);
        }   break;
        case OFX::eBitDepthUShort :
        {
            CropProcessor<unsigned short, nComponents, 65535> fred(*this);
            setupAndProcess(fred, args);
        }   break;
        case OFX::eBitDepthFloat :
        {
            CropProcessor<float, nComponents, 1> fred(*this);
            setupAndProcess(fred, args);
        }   break;
        default :
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
CropPlugin::render(const OFX::RenderArguments &args)
{
    
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();

    assert(dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA || dstComponents == OFX::ePixelComponentAlpha);
    if (dstComponents == OFX::ePixelComponentRGBA) {
        renderInternal<4>(args, dstBitDepth);
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        renderInternal<3>(args, dstBitDepth);
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
    
    virtual OfxPointD getBtmLeft(OfxTime time) const /*OVERRIDE FINAL*/ {
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
    
    virtual void aboutToCheckInteractivity(OfxTime time) {
        _reformat->getValueAtTime(time,_isReformated);
    }
    
    virtual bool allowTopLeftInteraction() const { return !_isReformated; }
    virtual bool allowBtmRightInteraction() const { return !_isReformated; }
    virtual bool allowBtmLeftInteraction() const { return !_isReformated; }
    virtual bool allowBtmMidInteraction() const { return !_isReformated; }
    virtual bool allowMidLeftInteraction() const { return !_isReformated; }
    virtual bool allowCenterInteraction() const { return !_isReformated; }


    OFX::BooleanParam* _reformat;
    bool _isReformated; //< @see aboutToCheckInteractivity
};

class CropOverlayDescriptor : public DefaultEffectOverlayDescriptor<CropOverlayDescriptor, CropInteract> {};



mDeclarePluginFactory(CropPluginFactory, {}, {});

void CropPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
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
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(kRenderThreadSafety);
    
    desc.setSupportsTiles(kSupportsTiles);
    
    // in order to support multiresolution, render() must take into account the pixelaspectratio and the renderscale
    // and scale the transform appropriately.
    // All other functions are usually in canonical coordinates.
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setOverlayInteractDescriptor(new CropOverlayDescriptor);

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
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // btmLeft
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamRectangleInteractBtmLeft);
        param->setLabels(kParamRectangleInteractBtmLeftLabel,kParamRectangleInteractBtmLeftLabel,kParamRectangleInteractBtmLeftLabel);
        param->setDoubleType(OFX::eDoubleTypeXYAbsolute);
        param->setDefaultCoordinateSystem(OFX::eCoordinatesNormalised);
        param->setDefault(0., 0.);
        param->setIncrement(1.);
        param->setHint("Coordinates of the bottom left corner of the crop rectangle");
        param->setDigits(0);
        page->addChild(*param);
    }

    // size
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamRectangleInteractSize);
        param->setLabels(kParamRectangleInteractSizeLabel, kParamRectangleInteractSizeLabel, kParamRectangleInteractSizeLabel);
        param->setDoubleType(OFX::eDoubleTypeXYAbsolute);
        param->setDefaultCoordinateSystem(OFX::eCoordinatesNormalised);
        param->setDefault(1., 1.);
        param->setIncrement(1.);
        param->setDimensionLabels("width", "height");
        param->setHint("Width and height of the crop rectangle");
        param->setIncrement(1.);
        param->setDigits(0);
        page->addChild(*param);
    }

    // softness
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSoftness);
        param->setLabels(kParamSoftnessLabel, kParamSoftnessLabel, kParamSoftnessLabel);
        param->setDefault(0);
        param->setRange(0., 1000.);
        param->setDisplayRange(0., 100.);
        param->setIncrement(1.);
        param->setHint("Size of the fade to black around edges to apply");
        page->addChild(*param);
    }

    // reformat
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamReformat);
        param->setLabels(kParamReformatLabel, kParamReformatLabel, kParamReformatLabel);
        param->setHint("Translates the bottom left corner of the crop rectangle to be in (0,0).");
        param->setDefault(false);
        param->setAnimates(true);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine);
        page->addChild(*param);
    }

    // intersect
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamIntersect);
        param->setLabels(kParamIntersectLabel, kParamIntersectLabel, kParamIntersectLabel);
        param->setHint("Intersects the crop rectangle with the input region of definition instead of extending it");
        param->setLayoutHint(OFX::eLayoutHintNoNewLine);
        param->setDefault(false);
        param->setAnimates(true);
        page->addChild(*param);
    }

    // blackOutside
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamBlackOutside);
        param->setLabels(kParamBlackOutsideLabel, kParamBlackOutsideLabel, kParamBlackOutsideLabel);
        param->setDefault(false);
        param->setAnimates(true);
        param->setHint("Add 1 black pixel to the region of definition so that all the area outside the crop rectangle is black");
        page->addChild(*param);
    }
}

void getCropPluginID(OFX::PluginFactoryArray &ids)
{
    static CropPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

