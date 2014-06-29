/*
 OFX Invert plugin.

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
 OFX Invert Example plugin, a plugin that illustrates the use of the OFX Support library.

 Copyright (C) 2007 The Open Effects Association Ltd
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

#include "Invert.h"

#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"

#include "ofxsProcessing.H"
#include "ofxsFilter.h"


#define kPluginName "InvertOFX"
#define kPluginGrouping "Color"
#define kPluginDescription "Inverse the selected channels"
#define kPluginIdentifier "net.sf.openfx:Invert"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kParamProcessR      "r"
#define kParamProcessRLabel "R"
#define kParamProcessRHint  "Invert red component"
#define kParamProcessG      "g"
#define kParamProcessGLabel "G"
#define kParamProcessGHint  "Invert green component"
#define kParamProcessB      "b"
#define kParamProcessBLabel "B"
#define kParamProcessBHint  "Invert blue component"
#define kParamProcessA      "a"
#define kParamProcessALabel "A"
#define kParamProcessAHint  "Invert alpha component"

using namespace OFX;

// Base class for the RGBA and the Alpha processor
class InvertBase : public OFX::ImageProcessor
{
protected:
    OFX::Image *_srcImg;
    OFX::Image *_maskImg;
    bool   _doMasking;
    bool _red;
    bool _green;
    bool _blue;
    bool _alpha;
    double _mix;
    bool _maskInvert;

public:
    /** @brief no arg ctor */
    InvertBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _maskImg(0)
    , _doMasking(false)
    , _red(true)
    , _green(true)
    , _blue(true)
    , _alpha(false)
    , _mix(1.)
    , _maskInvert(false)
    {
    }

    /** @brief set the src image */
    void setSrcImg(OFX::Image *v) {_srcImg = v;}

    void setMaskImg(OFX::Image *v) {_maskImg = v;}

    void doMasking(bool v) {_doMasking = v;}

    void setValues(bool red, bool green, bool blue, bool alpha, double mix, bool maskInvert)
    {
        _red = red;
        _green = green;
        _blue = blue;
        _alpha = alpha;
        _mix = mix;
        _maskInvert = maskInvert;
    }
};

// template to do the RGBA processing
template <class PIX, int nComponents, int maxValue>
class ImageInverter : public InvertBase
{
  public:
    // ctor
    ImageInverter(OFX::ImageEffect &instance)
            : InvertBase(instance)
    {
    }

  private:
    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        int todo = ((_red ? 0xf000 : 0) | (_green ? 0x0f00 : 0) | (_blue ? 0x00f0 : 0) | (_alpha ? 0x000f : 0));
        switch (todo) {
            case 0x0000:
                return process<false,false,false,false>(procWindow);
            case 0x000f:
                return process<false,false,false,true >(procWindow);
            case 0x00f0:
                return process<false,false,true ,false>(procWindow);
            case 0x00ff:
                return process<false,false,true, true >(procWindow);
            case 0x0f00:
                return process<false,true ,false,false>(procWindow);
            case 0x0f0f:
                return process<false,true ,false,true >(procWindow);
            case 0x0ff0:
                return process<false,true ,true ,false>(procWindow);
            case 0x0fff:
                return process<false,true ,true ,true >(procWindow);
            case 0xf000:
                return process<true ,false,false,false>(procWindow);
            case 0xf00f:
                return process<true ,false,false,true >(procWindow);
            case 0xf0f0:
                return process<true ,false,true ,false>(procWindow);
            case 0xf0ff:
                return process<true ,false,true, true >(procWindow);
            case 0xff00:
                return process<true ,true ,false,false>(procWindow);
            case 0xff0f:
                return process<true ,true ,false,true >(procWindow);
            case 0xfff0:
                return process<true ,true ,true ,false>(procWindow);
            case 0xffff:
                return process<true ,true ,true ,true >(procWindow);
        }
    }

  private:
    template<bool dored, bool dogreen, bool doblue, bool doalpha>
    void process(const OfxRectI& procWindow)
    {
        float tmpPix[nComponents];
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if (_effect.abort()) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {

                PIX *srcPix = (PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);

                // do we have a source image to scale up
                if (srcPix) {
                    switch (nComponents) {
                        case 1: // Alpha
                            tmpPix[0] = doalpha ? (maxValue - srcPix[0]) : srcPix[0];
                            break;
                        case 3: // RGB
                            tmpPix[0] = dored   ? (maxValue - srcPix[0]) : srcPix[0];
                            tmpPix[1] = dogreen ? (maxValue - srcPix[1]) : srcPix[1];
                            tmpPix[2] = doblue  ? (maxValue - srcPix[2]) : srcPix[2];
                            break;
                        case 4: // RGBA
                            tmpPix[0] = dored   ? (maxValue - srcPix[0]) : srcPix[0];
                            tmpPix[1] = dogreen ? (maxValue - srcPix[1]) : srcPix[1];
                            tmpPix[2] = doblue  ? (maxValue - srcPix[2]) : srcPix[2];
                            tmpPix[3] = doalpha ? (maxValue - srcPix[3]) : srcPix[3];
                            break;
                    }
                    ofxsMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, x, y, srcPix, _doMasking, _maskImg, _mix, _maskInvert, dstPix);
                } else {
                    // no src pixel here, be black and transparent
                    for (int c = 0; c < nComponents; c++) {
                        dstPix[c] = 0;
                    }
                }

                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    }
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class InvertPlugin : public OFX::ImageEffect
{
  public:
    /** @brief ctor */
    InvertPlugin(OfxImageEffectHandle handle)
            : ImageEffect(handle)
            , dstClip_(0)
            , srcClip_(0)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        assert(dstClip_ && (dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA || dstClip_->getPixelComponents() == ePixelComponentAlpha));
        srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(srcClip_ && (srcClip_->getPixelComponents() == ePixelComponentRGB || srcClip_->getPixelComponents() == ePixelComponentRGBA || srcClip_->getPixelComponents() == ePixelComponentAlpha));
        maskClip_ = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!maskClip_ || maskClip_->getPixelComponents() == ePixelComponentAlpha);
        _paramProcessR = fetchBooleanParam(kParamProcessR);
        _paramProcessG = fetchBooleanParam(kParamProcessG);
        _paramProcessB = fetchBooleanParam(kParamProcessB);
        _paramProcessA = fetchBooleanParam(kParamProcessA);
        _mix = fetchDoubleParam(kFilterMixParamName);
        _maskInvert = fetchBooleanParam(kFilterMaskInvertParamName);
    }

  private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) /*OVERRIDE FINAL*/;

    /* set up and run a processor */
    void setupAndProcess(InvertBase &, const OFX::RenderArguments &args);

    virtual bool isIdentity(const RenderArguments &args, Clip * &identityClip, double &identityTime) /*OVERRIDE FINAL*/;

  private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    OFX::Clip *maskClip_;

    OFX::BooleanParam* _paramProcessR;
    OFX::BooleanParam* _paramProcessG;
    OFX::BooleanParam* _paramProcessB;
    OFX::BooleanParam* _paramProcessA;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskInvert;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


/* set up and run a processor */
void
InvertPlugin::setupAndProcess(InvertBase &processor, const OFX::RenderArguments &args)
{
    // get a dst image
    std::auto_ptr<OFX::Image> dst(dstClip_->fetchImage(args.time));
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();

    // fetch main input image
    std::auto_ptr<OFX::Image> src(srcClip_->fetchImage(args.time));

    // make sure bit depths are sane
    if (src.get()) {
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();

        // see if they have the same depths and bytes and all
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    // auto ptr for the mask.
    std::auto_ptr<OFX::Image> mask((getContext() != OFX::eContextFilter) ? maskClip_->fetchImage(args.time) : 0);

    // do we do masking
    if (getContext() != OFX::eContextFilter && maskClip_->isConnected()) {
        // say we are masking
        processor.doMasking(true);

        // Set it in the processor
        processor.setMaskImg(mask.get());
    }

    bool red, green, blue, alpha;
    _paramProcessR->getValueAtTime(args.time, red);
    _paramProcessG->getValueAtTime(args.time, green);
    _paramProcessB->getValueAtTime(args.time, blue);
    _paramProcessA->getValueAtTime(args.time, alpha);
    double mix;
    _mix->getValueAtTime(args.time, mix);
    bool maskInvert;
    _maskInvert->getValueAtTime(args.time, maskInvert);
    processor.setValues(red, green, blue, alpha, mix, maskInvert);

    // set the images
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

// the overridden render function
void
InvertPlugin::render(const OFX::RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();

    // do the rendering
    if (dstComponents == OFX::ePixelComponentRGBA) {
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte : {
                ImageInverter<unsigned char, 4, 255> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthUShort : {
                ImageInverter<unsigned short, 4, 65535> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthFloat : {
                ImageInverter<float, 4, 1> fred(*this);
                setupAndProcess(fred, args);
            }
                break;
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte : {
                ImageInverter<unsigned char, 3, 255> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthUShort : {
                ImageInverter<unsigned short, 3, 65535> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthFloat : {
                ImageInverter<float, 3, 1> fred(*this);
                setupAndProcess(fred, args);
            }
                break;
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte : {
                ImageInverter<unsigned char, 1, 255> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthUShort : {
                ImageInverter<unsigned short, 1, 65535> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthFloat : {
                ImageInverter<float, 1, 1> fred(*this);
                setupAndProcess(fred, args);
            }
                break;
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}

bool
InvertPlugin::isIdentity(const RenderArguments &args, Clip * &identityClip, double &identityTime)
{
    bool red, green, blue, alpha;
    double mix;
    _paramProcessR->getValueAtTime(args.time, red);
    _paramProcessG->getValueAtTime(args.time, green);
    _paramProcessB->getValueAtTime(args.time, blue);
    _paramProcessA->getValueAtTime(args.time, alpha);
    _mix->getValueAtTime(args.time, mix);

    if (mix == 0. || (!red && !green && !blue && !alpha)) {
        identityClip = srcClip_;
        return true;
    } else {
        return false;
    }
}

mDeclarePluginFactory(InvertPluginFactory, {}, {});

using namespace OFX;
void InvertPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add the supported contexts
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextPaint);

    // add supported pixel depths
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

void InvertPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(true);
    srcClip->setIsMask(false);
    
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

    OFX::BooleanParamDescriptor* processR = desc.defineBooleanParam(kParamProcessR);
    processR->setLabels(kParamProcessRLabel, kParamProcessRLabel, kParamProcessRLabel);
    processR->setHint(kParamProcessRHint);
    processR->setDefault(true);
    processR->setLayoutHint(eLayoutHintNoNewLine);
    page->addChild(*processR);

    OFX::BooleanParamDescriptor* processG = desc.defineBooleanParam(kParamProcessG);
    processG->setLabels(kParamProcessGLabel, kParamProcessGLabel, kParamProcessGLabel);
    processG->setHint(kParamProcessGHint);
    processG->setDefault(true);
    processG->setLayoutHint(eLayoutHintNoNewLine);
    page->addChild(*processG);

    OFX::BooleanParamDescriptor* processB = desc.defineBooleanParam( kParamProcessB );
    processB->setLabels(kParamProcessBLabel, kParamProcessBLabel, kParamProcessBLabel);
    processB->setHint(kParamProcessBHint);
    processB->setDefault(true);
    processB->setLayoutHint(eLayoutHintNoNewLine);
    page->addChild(*processB);

    OFX::BooleanParamDescriptor* processA = desc.defineBooleanParam( kParamProcessA );
    processA->setLabels(kParamProcessALabel, kParamProcessALabel, kParamProcessALabel);
    processA->setHint(kParamProcessAHint);
    processA->setDefault(true);
    page->addChild(*processA);

    ofxsFilterDescribeParamsMaskMix(desc, page);
}

OFX::ImageEffect* InvertPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new InvertPlugin(handle);
}


void getInvertPluginID(OFX::PluginFactoryArray &ids)
{
    static InvertPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

