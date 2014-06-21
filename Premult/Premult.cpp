/*
 OFX Premult plugin.

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

#include "Premult.h"

#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"

#include "ofxsProcessing.H"


#define kPluginPremultName "PremultOFX"
#define kPluginPremultGrouping "Merge"
#define kPluginPremultDescription "Multiply the selected channels by alpha (or another channel)"
#define kPluginPremultIdentifier "net.sf.openfx:Premult"
#define kPluginUnpremultName "UnpremultOFX"
#define kPluginUnpremultGrouping "Merge"
#define kPluginUnpremultDescription "Divide the selected channels by alpha (or another channel)"
#define kPluginUnpremultIdentifier "net.sf.openfx:Unpremult"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kParamProcessR      "r"
#define kParamProcessRLabel "R"
#define kParamProcessRHint  "Multiply/divide red component"
#define kParamProcessG      "g"
#define kParamProcessGLabel "G"
#define kParamProcessGHint  "Multiply/divide green component"
#define kParamProcessB      "b"
#define kParamProcessBLabel "B"
#define kParamProcessBHint  "Multiply/divide blue component"
#define kParamProcessA      "a"
#define kParamProcessALabel "A"
#define kParamProcessAHint  "Multiply/divide alpha component"
#define kParamPremultName   "premultChannel"
#define kParamPremultLabel  "By"
#define kParamPremultHint   "Multiply/divide by this input channel"

#define kInputChannelNoneOption "None"
#define kInputChannelNoneHint "Don't multiply/divide"
#define kInputChannelROption "R"
#define kInputChannelRHint "R channel from input"
#define kInputChannelGOption "G"
#define kInputChannelGHint "G channel from input"
#define kInputChannelBOption "B"
#define kInputChannelBHint "B channel from input"
#define kInputChannelAOption "A"
#define kInputChannelAHint "A channel from input"

// TODO: sRGB conversions for short and byte types

enum InputChannelEnum {
    eInputChannelNone = 0,
    eInputChannelR,
    eInputChannelG,
    eInputChannelB,
    eInputChannelA,
};

using namespace OFX;

// Base class for the RGBA and the Alpha processor
class PremultBase : public OFX::ImageProcessor
{
  protected:
    OFX::Image *_srcImg;
    bool _red;
    bool _green;
    bool _blue;
    bool _alpha;
    int _p;
  public:
    /** @brief no arg ctor */
    PremultBase(OFX::ImageEffect &instance)
            : OFX::ImageProcessor(instance)
            , _srcImg(0)
            , _red(true)
            , _green(true)
            , _blue(true)
            , _alpha(false)
            , _p(3)
    {
    }

    /** @brief set the src image */
    void setSrcImg(OFX::Image *v) {_srcImg = v;}

    void setValues(bool red, bool green, bool blue, bool alpha, InputChannelEnum premultChannel)
    {
        _red = red;
        _green = green;
        _blue = blue;
        _alpha = alpha;
        switch (premultChannel) {
            case eInputChannelNone:
                _p = -1;
                break;
            case eInputChannelR:
                _p = 0;
                break;
            case eInputChannelG:
                _p = 1;
                break;
            case eInputChannelB:
                _p = 2;
                break;
            case eInputChannelA:
                _p = 3;
                break;
        }
    }
};

// template to do the RGBA processing
template <class PIX, int nComponents, int maxValue, bool isPremult>
class ImagePremulter : public PremultBase
{
  public:
    // ctor
    ImagePremulter(OFX::ImageEffect &instance)
            : PremultBase(instance)
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
                    if (_p >= 0 && (dored || dogreen || doblue || doalpha)) {
                        tmpPix[0] = dored   ? (isPremult ? ((srcPix[0]*srcPix[_p])/maxValue) : ((srcPix[0]*maxValue)/srcPix[_p])) : srcPix[0];
                        tmpPix[1] = dogreen ? (isPremult ? ((srcPix[1]*srcPix[_p])/maxValue) : ((srcPix[1]*maxValue)/srcPix[_p])) : srcPix[1];
                        tmpPix[2] = doblue  ? (isPremult ? ((srcPix[2]*srcPix[_p])/maxValue) : ((srcPix[2]*maxValue)/srcPix[_p])) : srcPix[2];
                        tmpPix[3] = doalpha ? (isPremult ? ((srcPix[3]*srcPix[_p])/maxValue) : ((srcPix[3]*maxValue)/srcPix[_p])) : srcPix[3];
                    } else {
                        for (int c = 0; c < nComponents; c++) {
                            dstPix[c] = srcPix[c];
                        }
                    }
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
template<bool isPremult>
class PremultPlugin : public OFX::ImageEffect
{
  public:
    /** @brief ctor */
    PremultPlugin(OfxImageEffectHandle handle)
            : ImageEffect(handle)
            , dstClip_(0)
            , srcClip_(0)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        assert(dstClip_ && (dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA || dstClip_->getPixelComponents() == ePixelComponentAlpha));
        srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(srcClip_ && (srcClip_->getPixelComponents() == ePixelComponentRGB || srcClip_->getPixelComponents() == ePixelComponentRGBA || srcClip_->getPixelComponents() == ePixelComponentAlpha));
        _paramProcessR = fetchBooleanParam(kParamProcessR);
        _paramProcessG = fetchBooleanParam(kParamProcessG);
        _paramProcessB = fetchBooleanParam(kParamProcessB);
        _paramProcessA = fetchBooleanParam(kParamProcessA);
        _paramPremult = fetchChoiceParam(kParamPremultName);
    }

  private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) /*OVERRIDE FINAL*/;

    /* set up and run a processor */
    void setupAndProcess(PremultBase &, const OFX::RenderArguments &args);

    virtual bool isIdentity(const RenderArguments &args, Clip * &identityClip, double &identityTime) /*OVERRIDE FINAL*/;

  private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;

    OFX::BooleanParam* _paramProcessR;
    OFX::BooleanParam* _paramProcessG;
    OFX::BooleanParam* _paramProcessB;
    OFX::BooleanParam* _paramProcessA;
    OFX::ChoiceParam* _paramPremult;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


/* set up and run a processor */
template<bool isPremult>
void
PremultPlugin<isPremult>::setupAndProcess(PremultBase &processor, const OFX::RenderArguments &args)
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

    bool red, green, blue, alpha;
    int premult_i;
    _paramProcessR->getValueAtTime(args.time, red);
    _paramProcessG->getValueAtTime(args.time, green);
    _paramProcessB->getValueAtTime(args.time, blue);
    _paramProcessA->getValueAtTime(args.time, alpha);
    _paramPremult->getValue(premult_i);
    InputChannelEnum premult = InputChannelEnum(premult_i);
    processor.setValues(red, green, blue, alpha, premult);

    // set the images
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

// the overridden render function
template<bool isPremult>
void
PremultPlugin<isPremult>::render(const OFX::RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();

    // do the rendering
    assert(dstComponents == OFX::ePixelComponentRGBA);
    switch (dstBitDepth) {
        case OFX::eBitDepthUByte : {
            ImagePremulter<unsigned char, 4, 255, isPremult> fred(*this);
            setupAndProcess(fred, args);
        }
            break;

        case OFX::eBitDepthUShort : {
            ImagePremulter<unsigned short, 4, 65535, isPremult> fred(*this);
            setupAndProcess(fred, args);
        }
            break;

        case OFX::eBitDepthFloat : {
            ImagePremulter<float, 4, 1, isPremult> fred(*this);
            setupAndProcess(fred, args);
        }
            break;
        default :
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

template<bool isPremult>
bool
PremultPlugin<isPremult>::isIdentity(const RenderArguments &args, Clip * &identityClip, double &identityTime)
{
    bool red, green, blue, alpha;
    int premult_i;
    _paramProcessR->getValueAtTime(args.time, red);
    _paramProcessG->getValueAtTime(args.time, green);
    _paramProcessB->getValueAtTime(args.time, blue);
    _paramProcessA->getValueAtTime(args.time, alpha);
    _paramPremult->getValueAtTime(args.time, premult_i);
    InputChannelEnum premult = InputChannelEnum(premult_i);

    if (premult == eInputChannelNone || (!red && !green && !blue && !alpha)) {
        identityClip = srcClip_;
        return true;
    } else {
        return false;
    }
}

//mDeclarePluginFactory(PremultPluginFactory, {}, {});

template<bool isPremult>
class PremultPluginFactory : public OFX::PluginFactoryHelper<PremultPluginFactory<isPremult> >
{
public:
    PremultPluginFactory(const std::string& id, unsigned int verMaj, unsigned int verMin):OFX::PluginFactoryHelper<PremultPluginFactory<isPremult> >(id, verMaj, verMin){}
    virtual void load() {};
    virtual void unload() {};
    virtual void describe(OFX::ImageEffectDescriptor &desc);
    virtual void describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context);
    virtual OFX::ImageEffect* createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context);
};

using namespace OFX;

template<bool isPremult>
void PremultPluginFactory<isPremult>::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    if (isPremult) {
        desc.setLabels(kPluginPremultName, kPluginPremultName, kPluginPremultName);
        desc.setPluginGrouping(kPluginPremultGrouping);
        desc.setPluginDescription(kPluginPremultDescription);
    } else {
        desc.setLabels(kPluginUnpremultName, kPluginUnpremultName, kPluginUnpremultName);
        desc.setPluginGrouping(kPluginUnpremultGrouping);
        desc.setPluginDescription(kPluginUnpremultDescription);
    }

    // add the supported contexts
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);

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

template<bool isPremult>
void PremultPluginFactory<isPremult>::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
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

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    OFX::BooleanParamDescriptor* processR = desc.defineBooleanParam(kParamProcessR);
    processR->setLabels(kParamProcessRLabel, kParamProcessRLabel, kParamProcessRLabel);
    processR->setHint(kParamProcessRHint);
    processR->setDefault(true);
    page->addChild(*processR);

    OFX::BooleanParamDescriptor* processG = desc.defineBooleanParam(kParamProcessG);
    processG->setLabels(kParamProcessGLabel, kParamProcessGLabel, kParamProcessGLabel);
    processG->setHint(kParamProcessGHint);
    processG->setDefault(true);
    page->addChild(*processG);

    OFX::BooleanParamDescriptor* processB = desc.defineBooleanParam( kParamProcessB );
    processB->setLabels(kParamProcessBLabel, kParamProcessBLabel, kParamProcessBLabel);
    processB->setHint(kParamProcessBHint);
    processB->setDefault(true);
    page->addChild(*processB);

    OFX::BooleanParamDescriptor* processA = desc.defineBooleanParam( kParamProcessA );
    processA->setLabels(kParamProcessALabel, kParamProcessALabel, kParamProcessALabel);
    processA->setHint(kParamProcessAHint);
    processA->setDefault(false);
    page->addChild(*processA);

    ChoiceParamDescriptor *premultChannel = desc.defineChoiceParam(kParamPremultName);
    premultChannel->setLabels(kParamPremultLabel, kParamPremultLabel, kParamPremultLabel);
    premultChannel->setHint(kParamPremultHint);
    assert(premultChannel->getNOptions() == eInputChannelNone);
    premultChannel->appendOption(kInputChannelNoneOption, kInputChannelNoneHint);
    assert(premultChannel->getNOptions() == eInputChannelR);
    premultChannel->appendOption(kInputChannelROption, kInputChannelRHint);
    assert(premultChannel->getNOptions() == eInputChannelG);
    premultChannel->appendOption(kInputChannelGOption, kInputChannelGHint);
    assert(premultChannel->getNOptions() == eInputChannelB);
    premultChannel->appendOption(kInputChannelBOption, kInputChannelBHint);
    assert(premultChannel->getNOptions() == eInputChannelA);
    premultChannel->appendOption(kInputChannelAOption, kInputChannelAHint);
    premultChannel->setDefault((int)eInputChannelA);
    page->addChild(*premultChannel);
}

template<bool isPremult>
OFX::ImageEffect*
PremultPluginFactory<isPremult>::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new PremultPlugin<isPremult>(handle);
}


void getPremultPluginID(OFX::PluginFactoryArray &ids)
{
    static PremultPluginFactory<true> p(kPluginPremultIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

void getUnpremultPluginID(OFX::PluginFactoryArray &ids)
{
    static PremultPluginFactory<false> p(kPluginUnpremultIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

