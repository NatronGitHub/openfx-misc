/*
 OFX CopyRectangle plugin.
 
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
#include "CopyRectangle.h"

#include <cmath>
#include <algorithm>

#include "ofxsProcessing.H"
#include "ofxsMerging.h"
#include "ofxsRectangleInteract.h"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"

#define kPluginName "CopyRectangleOFX"
#define kPluginGrouping "Merge"
#define kPluginDescription "Copies a rectangle from the input A to the input B in output. It can be used to limit an effect to a rectangle of the original image by plugging the original image into the input B."
#define kPluginIdentifier "net.sf.openfx.CopyRectanglePlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kClipA "A"
#define kClipB "B"
#define kParamSoftness "softness"
#define kParamSoftnessLabel "Softness"
#define kParamSoftnessHint "Size of the fade around edges of the rectangle to apply"
#define kParamRed "red"
#define kParamRedLabel "Red"
#define kParamGreen "green"
#define kParamGreenLabel "Green"
#define kParamBlue "blue"
#define kParamBlueLabel "Blue"
#define kParamAlpha "alpha"
#define kParamAlphaLabel "Alpha"


using namespace OFX;

class CopyRectangleProcessorBase : public OFX::ImageProcessor
{
   
    
protected:
    const OFX::Image *_srcImgA;
    const OFX::Image *_srcImgB;
    const OFX::Image *_maskImg;
    double _softness;
    bool _enabled[4];
    OfxRectI _rectangle;
    bool   _doMasking;
    double _mix;
    bool _maskInvert;

public:
    CopyRectangleProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImgA(0)
    , _srcImgB(0)
    , _maskImg(0)
    , _doMasking(false)
    , _mix(1.)
    , _maskInvert(false)
    {
    }

    /** @brief set the src image */
    void setSrcImgs(const OFX::Image *A, const OFX::Image* B)
    {
        _srcImgA = A;
        _srcImgB = B;
    }

    void setMaskImg(const OFX::Image *v) {_maskImg = v;}

    void doMasking(bool v) {_doMasking = v;}

    void setValues(const OfxRectI& rectangle,
                   double softness,
                   bool red,
                   bool green,
                   bool blue,
                   bool alpha,
                   double mix,
                   bool maskInvert)
    {
        _rectangle = rectangle;
        _softness = softness;
        _enabled[0] = red;
        _enabled[1] = green;
        _enabled[2] = blue;
        _enabled[3] = alpha;
        _mix = mix;
        _maskInvert = maskInvert;
    }

};


// The "masked", "filter" and "clamp" template parameters allow filter-specific optimization
// by the compiler, using the same generic code for all filters.
template <class PIX, int nComponents, int maxValue>
class CopyRectangleProcessor : public CopyRectangleProcessorBase
{
public:
    CopyRectangleProcessor(OFX::ImageEffect &instance)
    : CopyRectangleProcessorBase(instance)
    {
    }

private:
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        assert(nComponents == 1 || nComponents == 3 || nComponents == 4);
        double yMultiplier,xMultiplier;
        float tmpPix[nComponents];

        //assert(filter == _filter);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if (_effect.abort()) {
                break;
            }
            
            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            // distance to the nearest rectangle area horizontal edge
            int yDistance =  std::min(y - _rectangle.y1, _rectangle.y2 - 1 - y);
            
            // handle softness
            bool yInRectangle = y >= _rectangle.y1 && y < _rectangle.y2;
            if (yInRectangle) {
                ///apply softness only within the rectangle
                yMultiplier = yDistance < _softness ? (double)yDistance / _softness : 1.;
            } else {
                yMultiplier = 1.;
            }

            for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                
                // distance to the nearest rectangle area vertical edge
                int xDistance =  std::min(x - _rectangle.x1, _rectangle.x2 - 1 - x);
                // handle softness
                bool xInRectangle = x >= _rectangle.x1 && x < _rectangle.x2;
                
                if (xInRectangle) {
                    ///apply softness only within the rectangle
                    xMultiplier = xDistance < _softness ? (double)xDistance / _softness : 1.;
                } else {
                    xMultiplier = 1.;
                }
                
                const PIX *srcPixB = _srcImgB ?(const PIX*)_srcImgB->getPixelAddress(x, y) : NULL;

                if (xInRectangle && yInRectangle) {
                    const PIX *srcPixA = _srcImgA ? (const PIX*)_srcImgA->getPixelAddress(x, y) : NULL;

                    double multiplier = xMultiplier * yMultiplier;

                    for (int k = 0; k < nComponents; ++k) {
                        if (!_enabled[(nComponents) == 1 ? 3 : k]) {
                            tmpPix[k] = srcPixB ? srcPixB[k] : 0.;
                        } else {
                            PIX A = srcPixA ? srcPixA[k] : 0.;
                            PIX B = srcPixB ? srcPixB[k] : 0.;
                            tmpPix[k] = A *  multiplier + B * (1. - multiplier) ;
                        }
                    }
                    ofxsMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, x, y, srcPixB, _doMasking, _maskImg, _mix, _maskInvert, dstPix);
                } else {
                    for (int k = 0; k < nComponents; ++k) {
                        dstPix[k] = srcPixB ? srcPixB[k] : 0.;
                    }
                }
                
            }
        }
    }
};





////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class CopyRectanglePlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    CopyRectanglePlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClipA_(0)
    , srcClipB_(0)
    , _btmLeft(0)
    , _size(0)
    , _softness(0)
    , _red(0)
    , _green(0)
    , _blue(0)
    , _alpha(0)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        assert(dstClip_ && (dstClip_->getPixelComponents() == ePixelComponentAlpha || dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA));
        srcClipA_ = fetchClip(kClipA);
        assert(srcClipA_ && (srcClipA_->getPixelComponents() == ePixelComponentAlpha || srcClipA_->getPixelComponents() == ePixelComponentRGB || srcClipA_->getPixelComponents() == ePixelComponentRGBA));
        srcClipB_ = fetchClip(kClipB);
        assert(srcClipB_ && (srcClipB_->getPixelComponents() == ePixelComponentAlpha || srcClipB_->getPixelComponents() == ePixelComponentRGB || srcClipB_->getPixelComponents() == ePixelComponentRGBA));
        maskClip_ = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!maskClip_ || maskClip_->getPixelComponents() == ePixelComponentAlpha);

        _btmLeft = fetchDouble2DParam(kParamRectangleInteractBtmLeft);
        _size = fetchDouble2DParam(kParamRectangleInteractSize);
        _softness = fetchDoubleParam(kParamSoftness);
        _red = fetchBooleanParam(kParamRed);
        _green = fetchBooleanParam(kParamGreen);
        _blue = fetchBooleanParam(kParamBlue);
        _alpha = fetchBooleanParam(kParamAlpha);
        assert(_btmLeft && _size && _softness && _red && _green && _blue && _alpha);
        _mix = fetchDoubleParam(kParamMix);
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
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
    void setupAndProcess(CopyRectangleProcessorBase &, const OFX::RenderArguments &args);
    
    void getRectanglecanonical(OfxTime time,OfxRectD& rect) const;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClipA_;
    OFX::Clip *srcClipB_;
    OFX::Clip *maskClip_;

    OFX::Double2DParam* _btmLeft;
    OFX::Double2DParam* _size;
    OFX::DoubleParam* _softness;
    OFX::BooleanParam* _red;
    OFX::BooleanParam* _green;
    OFX::BooleanParam* _blue;
    OFX::BooleanParam* _alpha;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskInvert;
};

void
CopyRectanglePlugin::getRectanglecanonical(OfxTime time,OfxRectD& rect) const
{
    _btmLeft->getValueAtTime(time, rect.x1, rect.y1);
    double w,h;
    _size->getValueAtTime(time, w, h);
    rect.x2 = rect.x1 + w;
    rect.y2 = rect.y1 + h;

}

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
CopyRectanglePlugin::setupAndProcess(CopyRectangleProcessorBase &processor, const OFX::RenderArguments &args)
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
    std::auto_ptr<OFX::Image> srcA(srcClipA_->fetchImage(args.time));
    OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if (srcA.get()) {
        OFX::BitDepthEnum    srcBitDepth      = srcA->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = srcA->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        if (srcA->getRenderScale().x != args.renderScale.x ||
            srcA->getRenderScale().y != args.renderScale.y ||
            srcA->getField() != args.fieldToRender) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }
    std::auto_ptr<OFX::Image> srcB(srcClipB_->fetchImage(args.time));
    if (srcB.get()) {
        OFX::BitDepthEnum    srcBitDepth      = srcB->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = srcB->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        if (srcB->getRenderScale().x != args.renderScale.x ||
            srcB->getRenderScale().y != args.renderScale.y ||
            srcB->getField() != args.fieldToRender) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }
    std::auto_ptr<OFX::Image> mask(getContext() != OFX::eContextFilter ? maskClip_->fetchImage(args.time) : 0);
    if (mask.get()) {
        if (mask->getRenderScale().x != args.renderScale.x ||
            mask->getRenderScale().y != args.renderScale.y ||
            mask->getField() != args.fieldToRender) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }
    if (getContext() != OFX::eContextFilter && maskClip_->isConnected()) {
        processor.doMasking(true);
        processor.setMaskImg(mask.get());
    }

    // set the images
    processor.setDstImg(dst.get());
    processor.setSrcImgs(srcA.get(),srcB.get());
    
    // set the render window
    processor.setRenderWindow(args.renderWindow);
    
    OfxRectD rectangle;
    getRectanglecanonical(args.time, rectangle);
    OfxRectI rectanglePixel;
    rectanglePixel.x1 = rectangle.x1;
    rectanglePixel.y1 = rectangle.y1;
    rectanglePixel.x2 = rectangle.x2;
    rectanglePixel.y2 = rectangle.y2;
    
    unsigned int mipMapLevel = MergeImages2D::mipmapLevelFromScale(args.renderScale.x);
    rectanglePixel = MergeImages2D::downscalePowerOfTwoSmallestEnclosing(rectanglePixel, mipMapLevel);
    
    double softness;
    _softness->getValueAtTime(args.time, softness);
    softness *= args.renderScale.x;
    
    OfxRectD dstRoD = dstClip_->getRegionOfDefinition(args.time);
    OfxRectI dstRoDPix;
    dstRoDPix.x1 = dstRoD.x1;
    dstRoDPix.y1 = dstRoD.y1;
    dstRoDPix.x2 = dstRoD.x2;
    dstRoDPix.y2 = dstRoD.y2;
    dstRoDPix = MergeImages2D::downscalePowerOfTwoSmallestEnclosing(dstRoDPix, mipMapLevel);
   
    bool red,green,blue,alpha;
    _red->getValueAtTime(args.time,red);
    _green->getValueAtTime(args.time,green);
    _blue->getValueAtTime(args.time,blue);
    _alpha->getValueAtTime(args.time,alpha);
    
    double mix;
    _mix->getValueAtTime(args.time, mix);
    bool maskInvert;
    _maskInvert->getValueAtTime(args.time, maskInvert);
    processor.setValues(rectanglePixel, softness, red, green, blue, alpha, mix, maskInvert);
    
    // Call the base class process member, this will call the derived templated process code
    processor.process();
}



// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
void
CopyRectanglePlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois)
{

    OfxRectD rectangle;
    getRectanglecanonical(args.time, rectangle);

    // intersect the crop rectangle with args.regionOfInterest
    MergeImages2D::rectIntersection(rectangle, args.regionOfInterest, &rectangle);

    double mix;
    _mix->getValueAtTime(args.time, mix);
    if (mix != 1.) {
        // compute the bounding box with the default ROI
        MergeImages2D::rectBoundingBox(rectangle, args.regionOfInterest, &rectangle);
    }
    rois.setRegionOfInterest(*srcClipA_, rectangle);
    // no need to set the RoI on srcClipB_, since it's the same as the output RoI
}


bool
CopyRectanglePlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    OfxRectD rect;
    getRectanglecanonical(args.time, rect);
    OfxRectD srcB_rod = srcClipB_->getRegionOfDefinition(args.time);
    MergeImages2D::rectBoundingBox(rect, srcB_rod, &rod);
    return true;
}

// the internal render function
template <int nComponents>
void
CopyRectanglePlugin::renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth)
{
    switch(dstBitDepth) {
        case OFX::eBitDepthUByte :
        {
            CopyRectangleProcessor<unsigned char, nComponents, 255> fred(*this);
            setupAndProcess(fred, args);
        }   break;
        case OFX::eBitDepthUShort :
        {
            CopyRectangleProcessor<unsigned short, nComponents, 65535> fred(*this);
            setupAndProcess(fred, args);
        }   break;
        case OFX::eBitDepthFloat :
        {
            CopyRectangleProcessor<float, nComponents, 1> fred(*this);
            setupAndProcess(fred, args);
        }   break;
        default :
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

bool
CopyRectanglePlugin::isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &/*identityTime*/)
{
    double mix;
    _mix->getValueAtTime(args.time, mix);

    if (mix == 0.) {
        identityClip = srcClipB_;
        return true;
    } else {
        return false;
    }
}

// the overridden render function
void
CopyRectanglePlugin::render(const OFX::RenderArguments &args)
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



using namespace OFX;



mDeclarePluginFactory(CopyRectanglePluginFactory, {}, {});

void CopyRectanglePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);
    
    desc.addSupportedContext(eContextGeneral);

    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);
    
    
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(true);
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(eRenderFullySafe);
    
    desc.setSupportsTiles(true);
    
    // in order to support multiresolution, render() must take into account the pixelaspectratio and the renderscale
    // and scale the transform appropriately.
    // All other functions are usually in canonical coordinates.
    desc.setSupportsMultiResolution(true);
    desc.setOverlayInteractDescriptor(new RectangleOverlayDescriptor);

}



OFX::ImageEffect* CopyRectanglePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new CopyRectanglePlugin(handle);
}


static void defineComponentParam(OFX::ImageEffectDescriptor &desc,PageParamDescriptor* page,const std::string& name,const std::string& label)
{
    BooleanParamDescriptor* param = desc.defineBooleanParam(name);
    param->setLabels(label, label, label);
    param->setDefault(true);
    param->setHint("Copy " + name);
    page->addChild(*param);
}

void CopyRectanglePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    
    ClipDescriptor *srcClipB = desc.defineClip(kClipB);
    srcClipB->addSupportedComponent(ePixelComponentRGBA);
    srcClipB->addSupportedComponent(ePixelComponentRGB);
    srcClipB->addSupportedComponent(ePixelComponentAlpha);
    srcClipB->setTemporalClipAccess(false);
    srcClipB->setSupportsTiles(true);
    srcClipB->setIsMask(false);
    
    ClipDescriptor *srcClipA = desc.defineClip(kClipA);
    srcClipA->addSupportedComponent(ePixelComponentRGBA);
    srcClipA->addSupportedComponent(ePixelComponentRGB);
    srcClipA->addSupportedComponent(ePixelComponentAlpha);
    srcClipA->setTemporalClipAccess(false);
    srcClipA->setSupportsTiles(true);
    srcClipA->setIsMask(false);
    

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(true);

    if (context == eContextGeneral || context == eContextPaint) {
        ClipDescriptor *maskClip = context == eContextGeneral ? desc.defineClip("Mask") : desc.defineClip("Brush");
        maskClip->addSupportedComponent(ePixelComponentAlpha);
        maskClip->setTemporalClipAccess(false);
        if (context == eContextGeneral) {
            maskClip->setOptional(true);
        }
        maskClip->setSupportsTiles(true);
        maskClip->setIsMask(true);
    }

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
    
    // btmLeft
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamRectangleInteractBtmLeft);
        param->setLabels(kParamRectangleInteractBtmLeftLabel, kParamRectangleInteractBtmLeftLabel, kParamRectangleInteractBtmLeftLabel);
        param->setDoubleType(OFX::eDoubleTypeXYAbsolute);
        param->setDefaultCoordinateSystem(OFX::eCoordinatesNormalised);
        param->setDefault(0., 0.);
        param->setIncrement(1.);
        param->setHint(kParamRectangleInteractBtmLeftHint);
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
        param->setDimensionLabels(kParamRectangleInteractSizeDim1, kParamRectangleInteractSizeDim2);
        param->setHint(kParamRectangleInteractSizeHint);
        param->setIncrement(1.);
        param->setDigits(0);
        page->addChild(*param);
    }
    
    defineComponentParam(desc, page, kParamRed, kParamRedLabel);
    defineComponentParam(desc, page, kParamGreen, kParamGreenLabel);
    defineComponentParam(desc, page, kParamBlue, kParamBlueLabel);
    defineComponentParam(desc, page, kParamAlpha, kParamAlphaLabel);
    
    // softness
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSoftness);
        param->setLabels(kParamSoftnessLabel, kParamSoftnessLabel, kParamSoftnessLabel);
        param->setDefault(0);
        param->setRange(0., 100.);
        param->setIncrement(1.);
        param->setHint(kParamSoftnessHint);
        page->addChild(*param);
    }

    ofxsMaskMixDescribeParams(desc, page);
}

void getCopyRectanglePluginID(OFX::PluginFactoryArray &ids)
{
    static CopyRectanglePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

