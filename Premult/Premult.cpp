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

#include <limits>

#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"

#include "ofxsProcessing.H"
#include "ofxsMacros.h"


#define kPluginPremultName "PremultOFX"
#define kPluginPremultGrouping "Merge"
#define kPluginPremultDescription \
"Multiply the selected channels by alpha (or another channel).\n\n" \
"If no channel is selected, or the premultChannel is set to None, the " \
"image data is left untouched, but its premultiplication state is set to PreMultiplied."
#define kPluginPremultIdentifier "net.sf.openfx.Premult"
#define kPluginUnpremultName "UnpremultOFX"
#define kPluginUnpremultGrouping "Merge"
#define kPluginUnpremultDescription \
"Divide the selected channels by alpha (or another channel)\n\n" \
"If no channel is selected, or the premultChannel is set to None, the " \
"image data is left untouched, but its premultiplication state is set to UnPreMultiplied."
#define kPluginUnpremultIdentifier "net.sf.openfx.Unpremult"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe

#define kParamProcessR      "r"
#define kParamProcessRLabel "R"
#define kParamProcessRHint  " the red component"
#define kParamProcessG      "g"
#define kParamProcessGLabel "G"
#define kParamProcessGHint  "the green component"
#define kParamProcessB      "b"
#define kParamProcessBLabel "B"
#define kParamProcessBHint  " the blue component"
#define kParamProcessA      "a"
#define kParamProcessALabel "A"
#define kParamProcessAHint  " the alpha component"
#define kParamPremultName   "premultChannel"
#define kParamPremultLabel  "By"
#define kParamPremultHint   " by this input channel"

#define kParamPremultOptionNone "None"
#define kParamPremultOptionNoneHint "Don't multiply/divide"
#define kParamPremultOptionR "R"
#define kParamPremultOptionRHint "R channel from input"
#define kParamPremultOptionG "G"
#define kParamPremultOptionGHint "G channel from input"
#define kParamPremultOptionB "B"
#define kParamPremultOptionBHint "B channel from input"
#define kParamPremultOptionA "A"
#define kParamPremultOptionAHint "A channel from input"
#define kParamClipInfo "clipInfo"
#define kParamClipInfoLabel "Clip Info..."
#define kParamClipInfoHint "Display information about the inputs"

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
    const OFX::Image *_srcImg;
    bool _processR;
    bool _processG;
    bool _processB;
    bool _processA;
    int _p;
  public:
    /** @brief no arg ctor */
    PremultBase(OFX::ImageEffect &instance)
            : OFX::ImageProcessor(instance)
            , _srcImg(0)
            , _processR(true)
            , _processG(true)
            , _processB(true)
            , _processA(false)
            , _p(3)
    {
    }

    /** @brief set the src image */
    void setSrcImg(const OFX::Image *v) {_srcImg = v;}

    void setValues(bool processR, bool processG, bool processB, bool processA, InputChannelEnum premultChannel)
    {
        _processR = processR;
        _processG = processG;
        _processB = processB;
        _processA = processA;
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

template <class PIX, int maxValue>
static
PIX
ClampNonFloat(float v)
{
    if (maxValue == 1) {
        // assume float
        return v;
    }
    return (v > maxValue) ? maxValue : v;
}

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
        int todo = ((_processR ? 0xf000 : 0) | (_processG ? 0x0f00 : 0) | (_processB ? 0x00f0 : 0) | (_processA ? 0x000f : 0));
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
    template<bool processR, bool processG, bool processB, bool processA>
    void process(const OfxRectI& procWindow)
    {
        bool doc[4];
        doc[0] = processR;
        doc[1] = processG;
        doc[2] = processB;
        doc[3] = processA;
        const float fltmin = std::numeric_limits<float>::min();
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if (_effect.abort()) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {

                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);

                // do we have a source image to scale up
                if (srcPix) {
                    if (_p >= 0 && (processR || processG || processB || processA)) {
                        PIX alpha = srcPix[_p];
                        for (int c = 0; c < nComponents; c++) {
                            if (isPremult) {
                                dstPix[c] = doc[c] ? (((float)srcPix[c]*alpha)/maxValue) : srcPix[c];
                            } else {
                                PIX val;
                                if (!doc[c] || (alpha <= (PIX)(fltmin * maxValue))) {
                                    val = srcPix[c];
                                } else {
                                    val = ClampNonFloat<PIX, maxValue>(((float)srcPix[c]*maxValue)/alpha);
                                }
                                dstPix[c] = val;
                            }
                        }
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
            , _dstClip(0)
            , _srcClip(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == ePixelComponentRGB || _dstClip->getPixelComponents() == ePixelComponentRGBA || _dstClip->getPixelComponents() == ePixelComponentAlpha));
        _srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(_srcClip && (_srcClip->getPixelComponents() == ePixelComponentRGB || _srcClip->getPixelComponents() == ePixelComponentRGBA || _srcClip->getPixelComponents() == ePixelComponentAlpha));
        _processR = fetchBooleanParam(kParamProcessR);
        _processG = fetchBooleanParam(kParamProcessG);
        _processB = fetchBooleanParam(kParamProcessB);
        _processA = fetchBooleanParam(kParamProcessA);
        _premult = fetchChoiceParam(kParamPremultName);
    }

  private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(PremultBase &, const OFX::RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /* override changedParam */
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

  private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;

    OFX::BooleanParam* _processR;
    OFX::BooleanParam* _processG;
    OFX::BooleanParam* _processB;
    OFX::BooleanParam* _processA;
    OFX::ChoiceParam* _premult;
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
        dst->getField() != args.fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    // fetch main input image
    std::auto_ptr<const OFX::Image> src(_srcClip->fetchImage(args.time));

    // make sure bit depths are sane
    if (src.get()) {
        if (src->getRenderScale().x != args.renderScale.x ||
            src->getRenderScale().y != args.renderScale.y ||
            src->getField() != args.fieldToRender) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();

        // see if they have the same depths and bytes and all
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    bool processR, processG, processB, processA;
    int premult_i;
    _processR->getValueAtTime(args.time, processR);
    _processG->getValueAtTime(args.time, processG);
    _processB->getValueAtTime(args.time, processB);
    _processA->getValueAtTime(args.time, processA);
    _premult->getValue(premult_i);
    InputChannelEnum premult = InputChannelEnum(premult_i);
    processor.setValues(processR, processG, processB, processA, premult);

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
    OFX::BitDepthEnum       dstBitDepth    = _dstClip->getPixelDepth();

    // do the rendering
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
PremultPlugin<isPremult>::isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &/*identityTime*/)
{
    if (isPremult) {
        if (_srcClip->getPreMultiplication() != eImagePreMultiplied) {
            // input is UnPremult, output is Premult: no identity
            return false;
        }
    } else {
        if (_srcClip->getPreMultiplication() != eImageUnPreMultiplied) {
            // input is Premult, output is UnPremult: no identity
            return false;
        }
    }
    bool processR, processG, processB, processA;
    int premult_i;
    _processR->getValueAtTime(args.time, processR);
    _processG->getValueAtTime(args.time, processG);
    _processB->getValueAtTime(args.time, processB);
    _processA->getValueAtTime(args.time, processA);
    _premult->getValueAtTime(args.time, premult_i);
    InputChannelEnum premult = InputChannelEnum(premult_i);

    if (premult == eInputChannelNone || (!processR && !processG && !processB && !processA)) {
        // no processing: identity
        identityClip = _srcClip;
        return true;
    } else {
        // data is changed: no identity
        return false;
    }
}


/* Override the clip preferences */
template<bool isPremult>
void
PremultPlugin<isPremult>::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
#if 0
    // set the premultiplication of _dstClip
    bool processR, processG, processB, processA;
    int premult_i;
    _processR->getValue(processR);
    _processG->getValue(processG);
    _processB->getValue(processB);
    _processA->getValue(processA);
    _premult->getValue(premult_i);
    InputChannelEnum premult = InputChannelEnum(premult_i);

    if (premult == eInputChannelA && processR && processG && processB && !processA) {
        clipPreferences.setOutputPremultiplication(isPremult ? eImagePreMultiplied : eImageUnPreMultiplied);
    }
#else
    // Whatever the input is or the processed channels are, set the output premiltiplication.
    // This allows setting the output premult without changing the image data.
    clipPreferences.setOutputPremultiplication(isPremult ? eImagePreMultiplied : eImageUnPreMultiplied);
#endif
}

static std::string premultString(PreMultiplicationEnum e)
{
    switch (e) {
        case eImageOpaque:
            return "Opaque";
        case eImagePreMultiplied:
            return "PreMultiplied";
        case eImageUnPreMultiplied:
            return "UnPreMultiplied";
    }
    return "Unknown";
}

template<bool isPremult>
void
PremultPlugin<isPremult>::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kParamClipInfo && args.reason == eChangeUserEdit) {
        std::string msg;
        msg += "Input; ";
        if (!_srcClip) {
            msg += "N/A";
        } else {
            msg += premultString(_srcClip->getPreMultiplication());
        }
        msg += "\n";
        msg += "Output: ";
        if (!_dstClip) {
            msg += "N/A";
        } else {
            msg += premultString(_dstClip->getPreMultiplication());
        }
        msg += "\n";
        sendMessage(OFX::Message::eMessageMessage, "", msg);
    }
}

template<bool isPremult>
void
PremultPlugin<isPremult>::changedClip(const InstanceChangedArgs &args, const std::string &clipName)
{
    if (clipName == kOfxImageEffectSimpleSourceClipName && _srcClip && args.reason == OFX::eChangeUserEdit) {
        switch (_srcClip->getPreMultiplication()) {
            case eImageOpaque:
                break;
            case eImagePreMultiplied:
                if (isPremult) {
                    //_premult->setValue(eInputChannelNone);
                } else {
                    _processR->setValue(true);
                    _processG->setValue(true);
                    _processB->setValue(true);
                    _processA->setValue(false);
                    _premult->setValue(eInputChannelA);
                }
                break;
            case eImageUnPreMultiplied:
                if (!isPremult) {
                    //_premult->setValue(eInputChannelNone);
                } else {
                    _processR->setValue(true);
                    _processG->setValue(true);
                    _processB->setValue(true);
                    _processA->setValue(false);
                    _premult->setValue(eInputChannelA);
                }
                break;
        }
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
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(kRenderThreadSafety);
}

template<bool isPremult>
void PremultPluginFactory<isPremult>::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    //srcClip->addSupportedComponent(ePixelComponentRGB);
    //srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);
    
    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    //dstClip->addSupportedComponent(ePixelComponentRGB);
    //dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    const std::string premultString = isPremult ? "Multiply " : "Divide ";
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessR);
        param->setLabels(kParamProcessRLabel, kParamProcessRLabel, kParamProcessRLabel);
        param->setHint(premultString+kParamProcessRHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine);
        desc.addClipPreferencesSlaveParam(*param);
        page->addChild(*param);
    }

    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessG);
        param->setLabels(kParamProcessGLabel, kParamProcessGLabel, kParamProcessGLabel);
        param->setHint(premultString+kParamProcessGHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine);
        desc.addClipPreferencesSlaveParam(*param);
        page->addChild(*param);
    }

    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam( kParamProcessB );
        param->setLabels(kParamProcessBLabel, kParamProcessBLabel, kParamProcessBLabel);
        param->setHint(premultString+kParamProcessBHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine);
        desc.addClipPreferencesSlaveParam(*param);
        page->addChild(*param);
    }

    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam( kParamProcessA );
        param->setLabels(kParamProcessALabel, kParamProcessALabel, kParamProcessALabel);
        param->setHint(premultString+kParamProcessAHint);
        param->setDefault(false);
        param->setLayoutHint(eLayoutHintNoNewLine);
        desc.addClipPreferencesSlaveParam(*param);
        page->addChild(*param);
    }

    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamPremultName);
        param->setLabels(kParamPremultLabel, kParamPremultLabel, kParamPremultLabel);
        param->setHint(kParamPremultHint);
        assert(param->getNOptions() == eInputChannelNone);
        param->appendOption(kParamPremultOptionNone, kParamPremultOptionNoneHint);
        assert(param->getNOptions() == eInputChannelR);
        param->appendOption(kParamPremultOptionR, kParamPremultOptionRHint);
        assert(param->getNOptions() == eInputChannelG);
        param->appendOption(kParamPremultOptionG, kParamPremultOptionGHint);
        assert(param->getNOptions() == eInputChannelB);
        param->appendOption(kParamPremultOptionB, kParamPremultOptionBHint);
        assert(param->getNOptions() == eInputChannelA);
        param->appendOption(kParamPremultOptionA, kParamPremultOptionAHint);
        param->setDefault((int)eInputChannelA);
        desc.addClipPreferencesSlaveParam(*param);
        page->addChild(*param);
    }

    {
    PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamClipInfo);
    param->setLabels(kParamClipInfoLabel, kParamClipInfoLabel, kParamClipInfoLabel);
    param->setHint(kParamClipInfoHint);
    page->addChild(*param);
    }
}

template<bool isPremult>
OFX::ImageEffect*
PremultPluginFactory<isPremult>::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new PremultPlugin<isPremult>(handle);
}


void getPremultPluginIDs(OFX::PluginFactoryArray &ids)
{
    {
        static PremultPluginFactory<true> p(kPluginPremultIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
    {
        static PremultPluginFactory<false> p(kPluginUnpremultIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
}

