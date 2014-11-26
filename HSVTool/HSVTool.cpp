/*
 OFX HSVTool plugin.
 
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

#include "HSVTool.h"

#include <cmath>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"

#define kPluginName "HSVToolOFX"
#define kPluginGrouping "Color"
#define kPluginDescription "Adjust hue, saturation and brightnes, or perform color replacement."
#define kPluginIdentifier "net.sf.openfx.HSVToolPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe


#pragma message WARN("TODO")

#define kParamClampBlack "clampBlack"
#define kParamClampBlackLabel "Clamp Black"
#define kParamClampBlackHint "All colors below 0 on output are set to 0."

#define kParamClampWhite "clampWhite"
#define kParamClampWhiteLabel "Clamp White"
#define kParamClampWhiteHint "All colors above 1 on output are set to 1."


using namespace OFX;

/* algorithm:
 - convert to HSV
 - compute H, S, and V coefficients: 1 within range, dropping to 0 at range+-rolloff
 - multiply the three coeffs. coeff = hcoeff*scoeff*vcoeff
 - if global coeff is 0, don't change anything.
 - else, adjust hue by hueRotation*coeff, etc.
 - convert back to RGB
 
 - when setting srcColor: compute hueRange, satRange, valRange (as empty ranges), set rolloffs to (50,0.3,0.3)
 - when setting dstColor: compute hueRotation, satAdjust and valAdjust
 */
struct HSVToolValues {
    double srcColor[3];
    double dstColor[3];
    double hueRange[2];
    double hueRotation;
    double hueRolloff;
    double satRange[2];
    double satAdjust;
    double satRolloff;
    double valRange[2];
    double valAdjust;
    double valRolloff;
};

class HSVToolProcessorBase : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImg;
    const OFX::Image *_maskImg;
    bool _premult;
    int _premultChannel;
    bool   _doMasking;
    double _mix;
    bool _maskInvert;

public:
    
    HSVToolProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _maskImg(0)
    , _premult(false)
    , _premultChannel(3)
    , _doMasking(false)
    , _mix(1.)
    , _maskInvert(false)
    , _clampBlack(true)
    , _clampWhite(true)
    {
    }
    
    void setSrcImg(const OFX::Image *v) {_srcImg = v;}
    
    void setMaskImg(const OFX::Image *v, bool maskInvert) { _maskImg = v; _maskInvert = maskInvert; }
    
    void doMasking(bool v) {_doMasking = v;}
    
    void setValues(const HSVToolValues& values,
                   bool clampBlack,
                   bool clampWhite,
                   bool premult,
                   int premultChannel,
                   double mix)
    {
        _values = values;
        _clampBlack = clampBlack;
        _clampWhite = clampWhite;
        _premult = premult;
        _premultChannel = premultChannel;
        _mix = mix;
    }

    void hsvtool(float r, float g, float b, float *rout, double *gout, double *bout)
    {
#pragma message WARN("TODO")

    }

private:
    HSVToolValues _values;
    bool _clampBlack;
    bool _clampWhite;
};



template <class PIX, int nComponents, int maxValue>
class HSVToolProcessor : public HSVToolProcessorBase
{
public:
    HSVToolProcessor(OFX::ImageEffect &instance)
    : HSVToolProcessorBase(instance)
    {
    }
    
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        assert(nComponents == 3 || nComponents == 4);
        assert(_dstImg);
        float unpPix[4];
        float tmpPix[4];
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if (_effect.abort()) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                ofxsUnPremult<PIX, nComponents, maxValue>(srcPix, unpPix, _premult, _premultChannel);
#pragma message WARN("TODO")
                tmpPix[0] = unpPix[0];
                tmpPix[1] = unpPix[1];
                tmpPix[2] = unpPix[2];
                tmpPix[3] = unpPix[3];
                ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, _premult, _premultChannel, x, y, srcPix, _doMasking, _maskImg, _mix, _maskInvert, dstPix);
                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    }
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class HSVToolPlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    HSVToolPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    , maskClip_(0)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        assert(dstClip_ && (dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA));
        srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(srcClip_ && (srcClip_->getPixelComponents() == ePixelComponentRGB || srcClip_->getPixelComponents() == ePixelComponentRGBA));
        maskClip_ = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!maskClip_ || maskClip_->getPixelComponents() == ePixelComponentAlpha);

#pragma message WARN("TODO")

        _clampBlack = fetchBooleanParam(kParamClampBlack);
        _clampWhite = fetchBooleanParam(kParamClampWhite);
        assert(_clampBlack && _clampWhite);
        _premult = fetchBooleanParam(kParamPremult);
        _premultChannel = fetchChoiceParam(kParamPremultChannel);
        assert(_premult && _premultChannel);
        _mix = fetchDoubleParam(kParamMix);
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
    }
    
private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;
    
    /* set up and run a processor */
    void setupAndProcess(HSVToolProcessorBase &, const OFX::RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    OFX::Clip *maskClip_;
#pragma message WARN("TODO")
    OFX::BooleanParam* _clampBlack;
    OFX::BooleanParam* _clampWhite;
    OFX::BooleanParam* _premult;
    OFX::ChoiceParam* _premultChannel;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskInvert;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
HSVToolPlugin::setupAndProcess(HSVToolProcessorBase &processor, const OFX::RenderArguments &args)
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
    OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
    std::auto_ptr<OFX::Image> src(srcClip_->fetchImage(args.time));
    if (src.get()) {
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    std::auto_ptr<OFX::Image> mask(getContext() != OFX::eContextFilter ? maskClip_->fetchImage(args.time) : 0);
    if (getContext() != OFX::eContextFilter && maskClip_->isConnected()) {
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }
    
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());
    processor.setRenderWindow(args.renderWindow);

    HSVToolValues values;
#pragma message WARN("TODO")

    bool clampBlack,clampWhite;
    _clampBlack->getValueAtTime(args.time, clampBlack);
    _clampWhite->getValueAtTime(args.time, clampWhite);
    bool premult;
    int premultChannel;
    _premult->getValueAtTime(args.time, premult);
    _premultChannel->getValueAtTime(args.time, premultChannel);
    double mix;
    _mix->getValueAtTime(args.time, mix);
    
    processor.setValues(values, clampBlack, clampWhite, premult, premultChannel, mix);
    processor.process();
}

// the overridden render function
void
HSVToolPlugin::render(const OFX::RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();
    
    assert(dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA);
    if (dstComponents == OFX::ePixelComponentRGBA) {
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte :
            {
                HSVToolProcessor<unsigned char, 4, 255> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort :
            {
                HSVToolProcessor<unsigned short, 4, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat :
            {
                HSVToolProcessor<float,4,1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else {
        assert(dstComponents == OFX::ePixelComponentRGB);
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte :
            {
                HSVToolProcessor<unsigned char, 3, 255> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort :
            {
                HSVToolProcessor<unsigned short, 3, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat :
            {
                HSVToolProcessor<float,3,1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}


bool
HSVToolPlugin::isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &/*identityTime*/)
{
    double mix;
    _mix->getValueAtTime(args.time, mix);

    if (mix == 0.) {
        identityClip = srcClip_;
        return true;
    }

#pragma message WARN("TODO")
    // isIdentity=true if hueRotation, satAdjust and valAdjust = 0.

    return false;
}

void
HSVToolPlugin::changedClip(const InstanceChangedArgs &args, const std::string &clipName)
{
    if (clipName == kOfxImageEffectSimpleSourceClipName && srcClip_ && args.reason == OFX::eChangeUserEdit) {
        switch (srcClip_->getPreMultiplication()) {
            case eImageOpaque:
                _premult->setValue(false);
                break;
            case eImagePreMultiplied:
                _premult->setValue(true);
                break;
            case eImageUnPreMultiplied:
                _premult->setValue(false);
                break;
        }
    }
}

mDeclarePluginFactory(HSVToolPluginFactory, {}, {});

void
HSVToolPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextPaint);
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
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(kRenderThreadSafety);
    
}

void
HSVToolPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);
    
    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->setSupportsTiles(kSupportsTiles);
    
    if (context == eContextGeneral || context == eContextPaint) {
        ClipDescriptor *maskClip = context == eContextGeneral ? desc.defineClip("Mask") : desc.defineClip("Brush");
        maskClip->addSupportedComponent(ePixelComponentAlpha);
        maskClip->setTemporalClipAccess(false);
        if (context == eContextGeneral)
            maskClip->setOptional(true);
        maskClip->setSupportsTiles(kSupportsTiles);
        maskClip->setIsMask(true);
    }
    
    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
    

    
#pragma message WARN("TODO")


    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamClampBlack);
        param->setLabels(kParamClampBlackLabel, kParamClampBlackLabel, kParamClampBlackLabel);
        param->setHint(kParamClampBlackHint);
        param->setDefault(true);
        param->setAnimates(true);
        page->addChild(*param);
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamClampWhite);
        param->setLabels(kParamClampWhiteLabel, kParamClampWhiteLabel, kParamClampWhiteLabel);
        param->setHint(kParamClampWhiteHint);
        param->setDefault(false);
        param->setAnimates(true);
        page->addChild(*param);
    }
    
    ofxsPremultDescribeParams(desc, page);
    ofxsMaskMixDescribeParams(desc, page);
}

OFX::ImageEffect*
HSVToolPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new HSVToolPlugin(handle);
}

void getHSVToolPluginID(OFX::PluginFactoryArray &ids)
{
    static HSVToolPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

