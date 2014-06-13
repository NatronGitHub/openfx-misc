/*
 OFX Shuffle plugin.

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

#include "Shuffle.h"

#include <cmath>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"

#define kShufflePluginLabel "ShuffleOFX"
#define kShufflePluginGrouping "Channel"
#define kShufflePluginDescription "Rearrange channels from one or two inputs and/or convert to different bit depth or components."


#define kOutputComponentsParamName "outputComponents"
#define kOutputComponentsParamLabel "Output Components"
#define kOutputComponentsParamHint "Components in the output"
#define kOutputComponentsRGBAOption "RGBA"
#define kOutputComponentsRGBOption "RGB"
#define kOutputComponentsAlphaOption "Alpha"
#define kOutputBitDepthParamName "outputBitDepth"
#define kOutputBitDepthParamLabel "Output Bit Depth"
#define kOutputBitDepthParamHint "Bit depth of the output"
#define kOutputBitDepthByteOption "Byte (8 bits)"
#define kOutputBitDepthShortOption "Short (16 bits)"
#define kOutputBitDepthFloatOption "Float (32 bits)"
#define kOutputRParamName "outputR"
#define kOutputRParamLabel "Output R"
#define kOutputRParamHint "Input channel for the output red channel"
#define kOutputGParamName "outputG"
#define kOutputGParamLabel "Output G"
#define kOutputGParamHint "Input channel for the output green channel"
#define kOutputBParamName "outputB"
#define kOutputBParamLabel "Output B"
#define kOutputBParamHint "Input channel for the output blue channel"
#define kOutputAParamName "outputA"
#define kOutputAParamLabel "Output A"
#define kOutputAParamHint "Input channel for the output alpha channel"

#define kInputChannelAROption "A.r"
#define kInputChannelARHint "R channel from input A"
#define kInputChannelAGOption "A.g"
#define kInputChannelAGHint "G channel from input A"
#define kInputChannelABOption "A.b"
#define kInputChannelABHint "B channel from input A"
#define kInputChannelAAOption "A.a"
#define kInputChannelAAHint "A channel from input A"
#define kInputChannel0Option "0"
#define kInputChannel0Hint "0 constant channel"
#define kInputChannel1Option "1"
#define kInputChannel1Hint "1 constant channel"
#define kInputChannelBROption "B.r"
#define kInputChannelBRHint "R channel from input B"
#define kInputChannelBGOption "B.g"
#define kInputChannelBGHint "G channel from input B"
#define kInputChannelBBOption "B.b"
#define kInputChannelBBHint "B channel from input B"
#define kInputChannelBAOption "B.a"
#define kInputChannelBAHint "A channel from input B"

// TODO: sRGB conversions for short and byte types

enum InputChannelEnum {
    eInputChannelAR = 0,
    eInputChannelAG,
    eInputChannelAB,
    eInputChannelAA,
    eInputChannel0,
    eInputChannel1,
    eInputChannelBR,
    eInputChannelBG,
    eInputChannelBB,
    eInputChannelBA,
};

#define kSourceClipAName "A"
#define kSourceClipBName "B"

static bool gSupportsBytes  = false;
static bool gSupportsShorts = false;
static bool gSupportsFloats = false;
static bool gSupportsRGBA   = false;
static bool gSupportsRGB    = false;
static bool gSupportsAlpha  = false;

static OFX::PixelComponentEnum gOutputComponentsMap[4];
static OFX::BitDepthEnum gOutputBitDepthMap[4];

using namespace OFX;


class ShufflerBase : public OFX::ImageProcessor
{
protected:
    OFX::Image *_srcImgA;
    OFX::Image *_srcImgB;
    PixelComponentEnum _outputComponents;
    BitDepthEnum _outputBitDepth;
    InputChannelEnum _r;
    InputChannelEnum _g;
    InputChannelEnum _b;
    InputChannelEnum _a;

    public :
    ShufflerBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImgA(0)
    , _srcImgB(0)
    {
    }

    void setSrcImg(OFX::Image *A, OFX::Image *B) {_srcImgA = A; _srcImgB = B;}

    void setValues(PixelComponentEnum outputComponents,
                   BitDepthEnum outputBitDepth,
                   InputChannelEnum r,
                   InputChannelEnum g,
                   InputChannelEnum b,
                   InputChannelEnum a)
    {
        _outputComponents = outputComponents,
        _outputBitDepth = outputBitDepth;
        _r = r;
        _g = g;
        _b = b;
        _a = a;
    }
};



template <class PIXSRC, class PIXDST, int nComponentsSrc, int nComponentsDst, int maxValueSrc, int maxValueDst>
class Shuffler : public ShufflerBase
{
    public :
    Shuffler(OFX::ImageEffect &instance)
    : ShufflerBase(instance)
    {
    }

    void multiThreadProcessImages(OfxRectI procWindow)
    {
        for(int y = procWindow.y1; y < procWindow.y2; y++) {
            PIXDST *dstPix = (PIXDST *) _dstImg->getPixelAddress(procWindow.x1, y);
            for(int x = procWindow.x1; x < procWindow.x2; x++) {
                PIXSRC *srcPixA = (PIXSRC *)  (_srcImgA ? _srcImgA->getPixelAddress(x, y) : 0);
                PIXSRC *srcPixB = (PIXSRC *)  (_srcImgB ? _srcImgB->getPixelAddress(x, y) : 0);
#warning "TODO: process"
                dstPix += nComponentsDst;
            }
        }
    }
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ShufflePlugin : public OFX::ImageEffect
{
    public :

    /** @brief ctor */
    ShufflePlugin(OfxImageEffectHandle handle, OFX::ContextEnum context)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClipA_(0)
    , srcClipB_(0)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        assert(dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA);
        srcClipA_ = fetchClip(context == eContextGeneral ? kSourceClipAName : kOfxImageEffectSimpleSourceClipName);
        assert(srcClipA_->getPixelComponents() == ePixelComponentRGB || srcClipA_->getPixelComponents() == ePixelComponentRGBA || srcClipA_->getPixelComponents() == ePixelComponentAlpha);
        if (context == eContextGeneral) {
            srcClipB_ = fetchClip(kSourceClipBName);
            assert(srcClipB_->getPixelComponents() == ePixelComponentRGB || srcClipB_->getPixelComponents() == ePixelComponentRGBA || srcClipB_->getPixelComponents() == ePixelComponentAlpha);
        }
        _outputComponents = fetchChoiceParam(kOutputComponentsParamName);
        _outputBitDepth = fetchChoiceParam(kOutputBitDepthParamName);
        _r = fetchChoiceParam(kOutputRParamName);
        _g = fetchChoiceParam(kOutputGParamName);
        _b = fetchChoiceParam(kOutputBParamName);
        _a = fetchChoiceParam(kOutputAParamName);
        enableComponents();
    }

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args);

    /** @brief get the clip preferences */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences);

    /* override changedParam */
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName);


    /* set up and run a processor */
    void setupAndProcess(ShufflerBase &, const OFX::RenderArguments &args);

private:
    void enableComponents(void);

    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClipA_;
    OFX::Clip *srcClipB_;

    OFX::ChoiceParam *_outputComponents;
    OFX::ChoiceParam *_outputBitDepth;
    OFX::ChoiceParam *_r;
    OFX::ChoiceParam *_g;
    OFX::ChoiceParam *_b;
    OFX::ChoiceParam *_a;
};

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

/* set up and run a processor */
void
ShufflePlugin::setupAndProcess(ShufflerBase &processor, const OFX::RenderArguments &args)
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
    std::auto_ptr<OFX::Image> srcA(srcClipA_->fetchImage(args.time));
    std::auto_ptr<OFX::Image> srcB(srcClipB_->fetchImage(args.time));
    if(srcA.get())
    {
        OFX::BitDepthEnum    srcBitDepth      = srcA->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = srcA->getPixelComponents();
        if(srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            throw int(1);
    }

    if(srcB.get())
    {
        OFX::BitDepthEnum    srcBitDepth      = srcB->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = srcB->getPixelComponents();
        if(srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            throw int(1);
    }

    int outputComponents_i;
    _outputComponents->getValue(outputComponents_i);
    PixelComponentEnum outputComponents = gOutputComponentsMap[outputComponents_i];
    int outputBitDepth_i;
    _outputBitDepth->getValue(outputBitDepth_i);
    BitDepthEnum outputBitDepth = gOutputBitDepthMap[outputBitDepth_i];
    int r_i;
    _r->getValue(r_i);
    InputChannelEnum r = InputChannelEnum(r_i);
    int g_i;
    _g->getValue(g_i);
    InputChannelEnum g = InputChannelEnum(g_i);
    int b_i;
    _b->getValue(b_i);
    InputChannelEnum b = InputChannelEnum(b_i);
    int a_i;
    _a->getValue(a_i);
    InputChannelEnum a = InputChannelEnum(a_i);

    processor.setValues(outputComponents, outputBitDepth, r, g, b, a);
    processor.setDstImg(dst.get());
    processor.setSrcImg(srcA.get(),srcB.get());
    processor.setRenderWindow(args.renderWindow);

    processor.process();
}

// the overridden render function
void
ShufflePlugin::render(const OFX::RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();
#warning "TODO: render"
}

/* Override the clip preferences */
void
ShufflePlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
    // set the components of dstClip_
    int outputComponents_i;
    _outputComponents->getValue(outputComponents_i);
    PixelComponentEnum outputComponents = gOutputComponentsMap[outputComponents_i];
    clipPreferences.setClipComponents(*dstClip_, outputComponents);

    // set the bitDepth of dstClip_
    int outputBitDepth_i;
    _outputBitDepth->getValue(outputBitDepth_i);
    BitDepthEnum outputBitDepth = gOutputBitDepthMap[outputBitDepth_i];
    clipPreferences.setClipBitDepth(*dstClip_, outputBitDepth);

}

void
ShufflePlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kOutputComponentsParamName) {
        enableComponents();
    }
}

void
ShufflePlugin::enableComponents(void)
{
    int outputComponents_i;
    _outputComponents->getValue(outputComponents_i);
    switch (gOutputComponentsMap[outputComponents_i]) {
        case ePixelComponentRGBA:
            _r->setEnabled(true);
            _g->setEnabled(true);
            _b->setEnabled(true);
            _a->setEnabled(true);
            break;
        case ePixelComponentRGB:
            _r->setEnabled(true);
            _g->setEnabled(true);
            _b->setEnabled(true);
            _a->setEnabled(false);
            break;
        case ePixelComponentAlpha:
            _r->setEnabled(false);
            _g->setEnabled(false);
            _b->setEnabled(false);
            _a->setEnabled(true);
            break;
        default:
            assert(0);
            break;
    }
}

void ShufflePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels(kShufflePluginLabel, kShufflePluginLabel, kShufflePluginLabel);
    desc.setPluginGrouping(kShufflePluginGrouping);
    desc.setPluginDescription(kShufflePluginDescription);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    if (getImageEffectHostDescription()->supportsMultipleClipDepths) {
        for (ImageEffectHostDescription::PixelDepthArray::const_iterator it = getImageEffectHostDescription()->_supportedPixelDepths.begin();
             it != getImageEffectHostDescription()->_supportedPixelDepths.end();
             ++it) {
            switch (*it) {
                case eBitDepthUByte:
                    gSupportsBytes  = true;
                    break;
                case eBitDepthUShort:
                    gSupportsShorts = true;
                    break;
                case eBitDepthFloat:
                    gSupportsFloats = true;
                    break;
                default:
                    // other bitdepths are not supported by this plugin
                    break;
            }
        }
    }
    {
        int i = 0;
        if (gSupportsFloats) {
            gOutputBitDepthMap[i] = eBitDepthFloat;
            ++i;
        }
        if (gSupportsShorts) {
            gOutputBitDepthMap[i] = eBitDepthUShort;
            ++i;
        }
        if (gSupportsBytes) {
            gOutputBitDepthMap[i] = eBitDepthUByte;
            ++i;
        }
         gOutputBitDepthMap[i] = eBitDepthNone;
    }
    for (ImageEffectHostDescription::PixelComponentArray::const_iterator it = getImageEffectHostDescription()->_supportedComponents.begin();
         it != getImageEffectHostDescription()->_supportedComponents.end();
         ++it) {
        switch (*it) {
            case ePixelComponentRGBA:
                gSupportsRGBA  = true;
                break;
            case ePixelComponentRGB:
                gSupportsRGB = true;
                break;
            case ePixelComponentAlpha:
                gSupportsAlpha = true;
                break;
            default:
                // other components are not supported by this plugin
                break;
        }
    }
    {
        int i = 0;
        if (gSupportsRGBA) {
            gOutputComponentsMap[i] = ePixelComponentRGBA;
            ++i;
        }
        if (gSupportsRGB) {
            gOutputComponentsMap[i] = ePixelComponentRGB;
            ++i;
        }
        if (gSupportsAlpha) {
            gOutputComponentsMap[i] = ePixelComponentAlpha;
            ++i;
        }
        gOutputComponentsMap[i] = ePixelComponentNone;
    }

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(true);
    desc.setSupportsTiles(true);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
    // say we can support multiple pixel depths on in and out
    desc.setSupportsMultipleClipDepths(true);
}

static void
addInputChannelOtions(ChoiceParamDescriptor* outputR, InputChannelEnum def, OFX::ContextEnum context)
{
    assert(outputR->getNOptions() == eInputChannelAR);
    outputR->appendOption(kInputChannelAROption,kInputChannelARHint);
    assert(outputR->getNOptions() == eInputChannelAG);
    outputR->appendOption(kInputChannelAGOption,kInputChannelAGHint);
    assert(outputR->getNOptions() == eInputChannelAB);
    outputR->appendOption(kInputChannelABOption,kInputChannelABHint);
    assert(outputR->getNOptions() == eInputChannelAA);
    outputR->appendOption(kInputChannelAAOption,kInputChannelAAHint);
    assert(outputR->getNOptions() == eInputChannel0);
    outputR->appendOption(kInputChannel0Option,kInputChannel0Hint);
    assert(outputR->getNOptions() == eInputChannel1);
    outputR->appendOption(kInputChannel1Option,kInputChannel1Hint);
    if (context == eContextGeneral) {
        assert(outputR->getNOptions() == eInputChannelBR);
        outputR->appendOption(kInputChannelBROption,kInputChannelBRHint);
        assert(outputR->getNOptions() == eInputChannelBG);
        outputR->appendOption(kInputChannelBGOption,kInputChannelBGHint);
        assert(outputR->getNOptions() == eInputChannelBB);
        outputR->appendOption(kInputChannelBBOption,kInputChannelBBHint);
        assert(outputR->getNOptions() == eInputChannelBA);
        outputR->appendOption(kInputChannelBAOption,kInputChannelBAHint);
    }
    outputR->setDefault(def);
}

void ShufflePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    if (context == eContextGeneral) {
        ClipDescriptor* srcClipB = desc.defineClip(kSourceClipBName);
        srcClipB->addSupportedComponent(ePixelComponentRGBA);
        srcClipB->addSupportedComponent(ePixelComponentRGB);
        srcClipB->addSupportedComponent(ePixelComponentAlpha);
        srcClipB->setTemporalClipAccess(false);
        srcClipB->setSupportsTiles(true);
        srcClipB->setOptional(true);

        ClipDescriptor* srcClipA = desc.defineClip(kSourceClipAName);
        srcClipA->addSupportedComponent(ePixelComponentRGBA);
        srcClipA->addSupportedComponent(ePixelComponentRGB);
        srcClipA->addSupportedComponent(ePixelComponentAlpha);
        srcClipA->setTemporalClipAccess(false);
        srcClipA->setSupportsTiles(true);
        srcClipA->setOptional(false);
    } else {
        ClipDescriptor* srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentRGB);
        srcClip->addSupportedComponent(ePixelComponentAlpha);
        srcClip->setTemporalClipAccess(false);
        srcClip->setSupportsTiles(true);
    }
    {
        // create the mandated output clip
        ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
        dstClip->addSupportedComponent(ePixelComponentRGBA);
        dstClip->addSupportedComponent(ePixelComponentRGB);
        dstClip->addSupportedComponent(ePixelComponentAlpha);
        dstClip->setSupportsTiles(true);
    }

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    ChoiceParamDescriptor *outputComponents = desc.defineChoiceParam(kOutputComponentsParamName);
    outputComponents->setLabels(kOutputComponentsParamLabel, kOutputComponentsParamLabel, kOutputComponentsParamLabel);
    outputComponents->setHint(kOutputComponentsParamHint);
    // the following must be in the same order as in describe(), so that the map works
    if (gSupportsRGBA) {
        assert(gOutputComponentsMap[outputComponents->getNOptions()] == ePixelComponentRGBA);
        outputComponents->appendOption(kOutputComponentsRGBAOption);
    }
    if (gSupportsRGB) {
        assert(gOutputComponentsMap[outputComponents->getNOptions()] == ePixelComponentRGB);
        outputComponents->appendOption(kOutputComponentsRGBOption);
    }
    if (gSupportsAlpha) {
        assert(gOutputComponentsMap[outputComponents->getNOptions()] == ePixelComponentAlpha);
        outputComponents->appendOption(kOutputComponentsAlphaOption);
    }
    outputComponents->setDefault(0);
    outputComponents->setAnimates(false);
    page->addChild(*outputComponents);

    if (getImageEffectHostDescription()->supportsMultipleClipDepths) {
        ChoiceParamDescriptor *outputBitDepth = desc.defineChoiceParam(kOutputBitDepthParamName);
        outputBitDepth->setLabels(kOutputBitDepthParamLabel, kOutputBitDepthParamLabel, kOutputBitDepthParamLabel);
        outputBitDepth->setHint(kOutputBitDepthParamHint);
        // the following must be in the same order as in describe(), so that the map works
        if (gSupportsFloats) {
            assert(gOutputBitDepthMap[outputBitDepth->getNOptions()] == eBitDepthFloat);
            outputBitDepth->appendOption(kOutputBitDepthFloatOption);
        }
        if (gSupportsShorts) {
            assert(gOutputBitDepthMap[outputBitDepth->getNOptions()] == eBitDepthUShort);
            outputBitDepth->appendOption(kOutputBitDepthShortOption);
        }
        if (gSupportsBytes) {
            assert(gOutputBitDepthMap[outputBitDepth->getNOptions()] == eBitDepthUByte);
            outputBitDepth->appendOption(kOutputBitDepthByteOption);
        }
        outputBitDepth->setDefault(0);
        outputBitDepth->setAnimates(false);
        page->addChild(*outputBitDepth);
    }

    if (gSupportsRGB || gSupportsRGBA) {
        ChoiceParamDescriptor *outputR = desc.defineChoiceParam(kOutputRParamName);
        outputR->setLabels(kOutputRParamLabel, kOutputRParamLabel, kOutputRParamLabel);
        outputR->setHint(kOutputRParamHint);
        addInputChannelOtions(outputR, eInputChannelAR, context);
        page->addChild(*outputR);
        ChoiceParamDescriptor *outputG = desc.defineChoiceParam(kOutputGParamName);
        outputG->setLabels(kOutputGParamLabel, kOutputGParamLabel, kOutputGParamLabel);
        outputG->setHint(kOutputGParamHint);
        addInputChannelOtions(outputG, eInputChannelAG, context);
        page->addChild(*outputG);
        ChoiceParamDescriptor *outputB = desc.defineChoiceParam(kOutputBParamName);
        outputB->setLabels(kOutputBParamLabel, kOutputBParamLabel, kOutputBParamLabel);
        outputB->setHint(kOutputBParamHint);
        addInputChannelOtions(outputB, eInputChannelAB, context);
        page->addChild(*outputB);
    }
    if (gSupportsRGBA || gSupportsAlpha) {
        ChoiceParamDescriptor *outputA = desc.defineChoiceParam(kOutputAParamName);
        outputA->setLabels(kOutputAParamLabel, kOutputAParamLabel, kOutputAParamLabel);
        outputA->setHint(kOutputAParamHint);
        addInputChannelOtions(outputA, eInputChannelAA, context);
        page->addChild(*outputA);
    }
}

OFX::ImageEffect* ShufflePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new ShufflePlugin(handle, context);
}

