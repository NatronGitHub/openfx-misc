/*
 OFX ColorMatrix plugin.
 
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

#include "ColorMatrix.h"

#include <cmath>
#include <cstring>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"

#define kPluginName "ColorMatrixOFX"
#define kPluginGrouping "Color"
#define kPluginDescription "Multiply the RGBA channels by an arbitrary 4x4 matrix."
#define kPluginIdentifier "net.sf.openfx:ColorMatrixPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kParamOutputRedName  "outputRed"
#define kParamOutputRedLabel "Output Red"
#define kParamOutputRedHint  "values for red output component."

#define kParamOutputGreenName  "outputGreen"
#define kParamOutputGreenLabel "Output Green"
#define kParamOutputGreenHint  "values for green output component."

#define kParamOutputBlueName  "outputBlue"
#define kParamOutputBlueLabel "Output Blue"
#define kParamOutputBlueHint  "values for blue output component."

#define kParamOutputAlphaName  "outputAlpha"
#define kParamOutputAlphaLabel "Output Alpha"
#define kParamOutputAlphaHint  "values for alpha output component."

#define kBlackPointParamName "blackPoint"
#define kBlackPointParamLabel "Black Point"
#define kWhitePointParamName "whitePoint"
#define kWhitePointParamLabel "White Point"
#define kBlackParamName "black"
#define kBlackParamLabel "Black"
#define kWhiteParamName "white"
#define kWhiteParamLabel "White"
#define kMultiplyParamName "multiply"
#define kMultiplyParamLabel "Multiply"
#define kOffsetParamName "offset"
#define kOffsetParamLabel "Offset"
#define kGammaParamName "gamma"
#define kGammaParamLabel "Gamma"
#define kClampBlackParamName "clampBlack"
#define kClampBlackParamLabel "Clamp Black"
#define kClampWhiteParamName "clampWhite"
#define kClampWhiteParamLabel "Clamp White"


using namespace OFX;


namespace {
    struct RGBAValues {
        double r,g,b,a;
    };
}

class ColorMatrixProcessorBase : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImg;
    const OFX::Image *_maskImg;
    RGBAValues _matrix[4];
    bool _clampBlack;
    bool _clampWhite;
    bool   _doMasking;
    double _mix;
    bool _maskInvert;

public:
    
    ColorMatrixProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _maskImg(0)
    , _doMasking(false)
    , _mix(1.)
    , _maskInvert(false)
    {
    }
    
    void setSrcImg(const OFX::Image *v) {_srcImg = v;}
    
    void setMaskImg(const OFX::Image *v) {_maskImg = v;}
    
    void doMasking(bool v) {_doMasking = v;}
    
    void setValues(const RGBAValues& outputRed,
                   const RGBAValues& outputGreen,
                   const RGBAValues& outputBlue,
                   const RGBAValues& outputAlpha,
                   bool clampBlack,
                   bool clampWhite,
                   double mix,
                   bool maskInvert)
    {
        _matrix[0] = outputRed;
        _matrix[1] = outputGreen;
        _matrix[2] = outputBlue;
        _matrix[3] = outputAlpha;
        _clampBlack = clampBlack;
        _clampWhite = clampWhite;
        _mix = mix;
        _maskInvert = maskInvert;
    }

private:
};



template <class PIX, int nComponents, int maxValue>
class ColorMatrixProcessor : public ColorMatrixProcessorBase
{
public:
    ColorMatrixProcessor(OFX::ImageEffect &instance)
    : ColorMatrixProcessorBase(instance)
    {
    }
    
private:

    double apply(int c, double inR, double inG, double inB, double inA) {
        double comp = _matrix[c].r * inR + _matrix[c].g * inG + _matrix[c].b * inB + _matrix[c].a * inA;
        if (_clampBlack && comp < 0.) {
            comp = 0.;
        } else  if (_clampWhite && comp > maxValue) {
            comp = maxValue;
        }
        return comp;
    }


    void multiThreadProcessImages(OfxRectI procWindow)
    {
        assert(nComponents == 3 || nComponents == 4);
        assert(_dstImg);
        float tmpPix[nComponents];
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if (_effect.abort()) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                if (srcPix) {
                    double inR = srcPix[0];
                    double inG = srcPix[1];
                    double inB = srcPix[2];
                    float inA = (nComponents == 4) ? srcPix[3] : 0.0;
                    for (int c = 0; c < nComponents; ++c) {
                        tmpPix[c] = apply(c, inR, inG, inB, inA);
                   }
                } else {
                    // no src pixel here, be black and transparent
                    std::memset(dstPix ,0, nComponents * sizeof(PIX));
                }
                ofxsMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, x, y, srcPix, _doMasking, _maskImg, _mix, _maskInvert, dstPix);
                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    }
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ColorMatrixPlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    ColorMatrixPlugin(OfxImageEffectHandle handle)
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
        _outputRed = fetchRGBAParam(kParamOutputRedName);
        _outputGreen = fetchRGBAParam(kParamOutputGreenName);
        _outputBlue = fetchRGBAParam(kParamOutputBlueName);
        _outputAlpha = fetchRGBAParam(kParamOutputAlphaName);
        _clampBlack = fetchBooleanParam(kClampBlackParamName);
        _clampWhite = fetchBooleanParam(kClampWhiteParamName);
        assert(_outputRed && _outputGreen && _outputBlue && _outputAlpha && _clampBlack && _clampWhite);
        _mix = fetchDoubleParam(kMixParamName);
        _maskInvert = fetchBooleanParam(kMaskInvertParamName);
        assert(_mix && _maskInvert);
    }
    
private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;
    
    /* set up and run a processor */
    void setupAndProcess(ColorMatrixProcessorBase &, const OFX::RenderArguments &args);

    virtual bool isIdentity(const RenderArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    OFX::Clip *maskClip_;
    OFX::RGBAParam *_outputRed;
    OFX::RGBAParam *_outputGreen;
    OFX::RGBAParam *_outputBlue;
    OFX::RGBAParam *_outputAlpha;
    OFX::BooleanParam* _clampBlack;
    OFX::BooleanParam* _clampWhite;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskInvert;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
ColorMatrixPlugin::setupAndProcess(ColorMatrixProcessorBase &processor, const OFX::RenderArguments &args)
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
        processor.doMasking(true);
        processor.setMaskImg(mask.get());
    }
    
    // set the images
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());
    // set the render window
    processor.setRenderWindow(args.renderWindow);
    
    RGBAValues r, g, b, a;
    _outputRed->getValueAtTime(args.time, r.r, r.g, r.b, r.a);
    _outputGreen->getValueAtTime(args.time, g.r, g.g, g.b, g.a);
    _outputBlue->getValueAtTime(args.time, b.r, b.g, b.b, b.a);
    _outputAlpha->getValueAtTime(args.time, a.r, a.g, a.b, a.a);
    bool clampBlack,clampWhite;
    _clampBlack->getValueAtTime(args.time, clampBlack);
    _clampWhite->getValueAtTime(args.time, clampWhite);
    double mix;
    _mix->getValueAtTime(args.time, mix);
    bool maskInvert;
    _maskInvert->getValueAtTime(args.time, maskInvert);
    processor.setValues(r, g, b, a, clampBlack, clampWhite, mix, maskInvert);
 
    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

// the overridden render function
void
ColorMatrixPlugin::render(const OFX::RenderArguments &args)
{
    
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();
    
    assert(dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA);
    if (dstComponents == OFX::ePixelComponentRGBA) {
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte:
            {
                ColorMatrixProcessor<unsigned char, 4, 255> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort:
            {
                ColorMatrixProcessor<unsigned short, 4, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat:
            {
                ColorMatrixProcessor<float,4,1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else {
        assert(dstComponents == OFX::ePixelComponentRGB);
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte:
            {
                ColorMatrixProcessor<unsigned char, 3, 255> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort:
            {
                ColorMatrixProcessor<unsigned short, 3, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat:
            {
                ColorMatrixProcessor<float,3,1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}


bool
ColorMatrixPlugin::isIdentity(const RenderArguments &args, Clip * &identityClip, double &identityTime)
{
    double mix;
    _mix->getValueAtTime(args.time, mix);

    if (mix == 0. /*|| (!red && !green && !blue && !alpha)*/) {
        identityClip = srcClip_;
        return true;
    }

    bool clampBlack,clampWhite;
    _clampBlack->getValueAtTime(args.time, clampBlack);
    _clampWhite->getValueAtTime(args.time, clampWhite);
    if (clampBlack || clampWhite) {
        return false;
    }
    RGBAValues r, g, b, a;
    _outputRed->getValueAtTime(args.time, r.r, r.g, r.b, r.a);
    _outputGreen->getValueAtTime(args.time, g.r, g.g, g.b, g.a);
    _outputBlue->getValueAtTime(args.time, b.r, b.g, b.b, b.a);
    _outputAlpha->getValueAtTime(args.time, a.r, a.g, a.b, a.a);
    if (r.r == 1. && r.g == 0. && r.b == 0. && r.a == 0. &&
        g.r == 0. && g.g == 1. && g.b == 0. && g.a == 0. &&
        b.r == 0. && b.g == 0. && b.b == 1. && b.a == 0. &&
        a.r == 0. && a.g == 0. && a.b == 0. && a.a == 1.) {
        return true;
    }
    return false;
}

mDeclarePluginFactory(ColorMatrixPluginFactory, {}, {});

void ColorMatrixPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
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
    desc.setSupportsMultiResolution(true);
    desc.setSupportsTiles(true);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
    
}

void ColorMatrixPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(true);
    srcClip->setIsMask(false);
    
    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->setSupportsTiles(true);
    
    if (context == eContextGeneral || context == eContextPaint) {
        ClipDescriptor *maskClip = context == eContextGeneral ? desc.defineClip("Mask") : desc.defineClip("Brush");
        maskClip->addSupportedComponent(ePixelComponentAlpha);
        maskClip->setTemporalClipAccess(false);
        if (context == eContextGeneral)
            maskClip->setOptional(true);
        maskClip->setSupportsTiles(true);
        maskClip->setIsMask(true);
    }
    
    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
    RGBAParamDescriptor *paramRed = desc.defineRGBAParam(kParamOutputRedName);
    paramRed->setLabels(kParamOutputRedLabel, kParamOutputRedLabel, kParamOutputRedLabel);
    paramRed->setHint(kParamOutputRedHint);
    paramRed->setDefault(1.0, 0.0, 0.0, 0.0);
    paramRed->setAnimates(true); // can animate
    page->addChild(*paramRed);

    RGBAParamDescriptor *paramGreen = desc.defineRGBAParam(kParamOutputGreenName);
    paramGreen->setLabels(kParamOutputGreenLabel, kParamOutputGreenLabel, kParamOutputGreenLabel);
    paramGreen->setHint(kParamOutputGreenHint);
    paramGreen->setDefault(0.0, 1.0, 0.0, 0.0);
    paramGreen->setAnimates(true); // can animate
    page->addChild(*paramGreen);

    RGBAParamDescriptor *paramBlue = desc.defineRGBAParam(kParamOutputBlueName);
    paramBlue->setLabels(kParamOutputBlueLabel, kParamOutputBlueLabel, kParamOutputBlueLabel);
    paramBlue->setHint(kParamOutputBlueHint);
    paramBlue->setDefault(0.0, 0.0, 1.0, 0.0);
    paramBlue->setAnimates(true); // can animate
    page->addChild(*paramBlue);

    RGBAParamDescriptor *paramAlpha = desc.defineRGBAParam(kParamOutputAlphaName);
    paramAlpha->setLabels(kParamOutputAlphaLabel, kParamOutputAlphaLabel, kParamOutputAlphaLabel);
    paramAlpha->setHint(kParamOutputAlphaHint);
    paramAlpha->setDefault(0.0, 0.0, 0.0, 1.0);
    paramAlpha->setAnimates(true); // can animate
    page->addChild(*paramAlpha);
    
    BooleanParamDescriptor *clampBlackParam = desc.defineBooleanParam(kClampBlackParamName);
    clampBlackParam->setLabels(kClampBlackParamLabel, kClampBlackParamLabel, kClampBlackParamLabel);
    clampBlackParam->setHint("All colors below 0 will be set to 0.");
    clampBlackParam->setDefault(true);
    clampBlackParam->setAnimates(true);
    page->addChild(*clampBlackParam);
    
    BooleanParamDescriptor *clampWhiteParam = desc.defineBooleanParam(kClampWhiteParamName);
    clampWhiteParam->setLabels(kClampWhiteParamLabel, kClampWhiteParamLabel, kClampWhiteParamLabel);
    clampWhiteParam->setHint("All colors above 1 will be set to 1.");
    clampWhiteParam->setDefault(true);
    clampWhiteParam->setAnimates(true);
    page->addChild(*clampWhiteParam);

    ofxsMaskMixDescribeParams(desc, page);
}

OFX::ImageEffect* ColorMatrixPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new ColorMatrixPlugin(handle);
}

void getColorMatrixPluginID(OFX::PluginFactoryArray &ids)
{
    static ColorMatrixPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

