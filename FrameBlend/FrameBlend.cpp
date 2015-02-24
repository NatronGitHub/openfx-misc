/*
 OFX FrameBlend plugin.

 Copyright (C) 2014 INRIA
 Author: Frederic Devernay <frederic.devernay@inria.fr>

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


 This plugin is based on:

 OFX retimer example plugin, a plugin that illustrates the use of the OFX Support library.

 This will not work very well on fielded imagery.

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

#include "FrameBlend.h"

#include <cmath> // for floor
#include <cfloat> // for FLT_MAX
#include <cassert>

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"

#define kPluginName "FrameBlendOFX"
#define kPluginGrouping "Time"
#define kPluginDescription "Blend frames of the input clip."
#define kPluginIdentifier "net.sf.openfx.FrameBlend"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamProcessR      "r"
#define kParamProcessRLabel "R"
#define kParamProcessRHint  "Process red component"
#define kParamProcessG      "g"
#define kParamProcessGLabel "G"
#define kParamProcessGHint  "Process green component"
#define kParamProcessB      "b"
#define kParamProcessBLabel "B"
#define kParamProcessBHint  "Process blue component"
#define kParamProcessA      "a"
#define kParamProcessALabel "A"
#define kParamProcessAHint  "Process alpha component"

#define kParamValueName  "value"
#define kParamValueLabel "Value"
#define kParamValueHint  "Constant to multiply with the selected channels."

#define kParamNbFramesName  "nbFrames"
#define kParamNbFramesLabel "Number of Frames"
#define kParamNbFramesHint  "Blend together nbFrames frames: the nbFrames-1 previous frames, and the current frame (when \"custom\" is not checked)."

#define kParamFrameRangeName  "frameRange"
#define kParamFrameRangeLabel "Frame Range"
#define kParamFrameRangeHint  "Range of frames which are to be blended together (when \"custom\" is checked)."

#define kParamCustomName  "custom"
#define kParamCustomLabel "Custom"
#define kParamCustomHint  "Use a custom frame range. If the frame range is not animated or is not an expression, then all output images will be the same."

#define kParamInputRangeName  "inputRange"
#define kParamInputRangeLabel "Input Range"
#define kParamInputRangeHint  "Set the frame range to the input range. This can be used, combined with a foreground matte, to produce a clean background plate."


using namespace OFX;


namespace {
    struct RGBAValues {
        double r,g,b,a;
        RGBAValues(double v) : r(v), g(v), b(v), a(v) {}
        RGBAValues() : r(0), g(0), b(0), a(0) {}
    };
}

class FrameBlendProcessorBase : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImg;
    const OFX::Image *_maskImg;
    bool _processR;
    bool _processG;
    bool _processB;
    bool _processA;
    RGBAValues _value;
    bool   _doMasking;
    double _mix;
    bool _maskInvert;

public:

    FrameBlendProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _maskImg(0)
    , _processR(true)
    , _processG(true)
    , _processB(true)
    , _processA(false)
    , _value()
    , _doMasking(false)
    , _mix(1.)
    , _maskInvert(false)
    {
    }

    void setSrcImg(const OFX::Image *v) {_srcImg = v;}

    void setMaskImg(const OFX::Image *v, bool maskInvert) { _maskImg = v; _maskInvert = maskInvert; }

    void doMasking(bool v) {_doMasking = v;}

    void setValues(bool processR,
                   bool processG,
                   bool processB,
                   bool processA,
                   const RGBAValues& value,
                   double mix)
    {
        _processR = processR;
        _processG = processG;
        _processB = processB;
        _processA = processA;
        _value = value;
        _mix = mix;
    }

private:
};



template <class PIX, int nComponents, int maxValue>
class FrameBlendProcessor : public FrameBlendProcessorBase
{
public:
    FrameBlendProcessor(OFX::ImageEffect &instance)
    : FrameBlendProcessorBase(instance)
    {
    }

private:

    void multiThreadProcessImages(OfxRectI procWindow)
    {
        int todo = ((_processR ? 0xf000 : 0) | (_processG ? 0x0f00 : 0) | (_processB ? 0x00f0 : 0) | (_processA ? 0x000f : 0));
        if (nComponents == 1) {
            switch (todo) {
                case 0x0000:
                case 0x00f0:
                case 0x0f00:
                case 0x0ff0:
                case 0xf000:
                case 0xf0f0:
                case 0xff00:
                case 0xfff0:
                    return process<false,false,false,false>(procWindow);
                case 0x000f:
                case 0x00ff:
                case 0x0f0f:
                case 0x0fff:
                case 0xf00f:
                case 0xf0ff:
                case 0xff0f:
                case 0xffff:
                    return process<false,false,false,true >(procWindow);
            }
        } else if (nComponents == 3) {
            switch (todo) {
                case 0x0000:
                case 0x000f:
                    return process<false,false,false,false>(procWindow);
                case 0x00f0:
                case 0x00ff:
                    return process<false,false,true ,false>(procWindow);
                case 0x0f00:
                case 0x0f0f:
                    return process<false,true ,false,false>(procWindow);
                case 0x0ff0:
                case 0x0fff:
                    return process<false,true ,true ,false>(procWindow);
                case 0xf000:
                case 0xf00f:
                    return process<true ,false,false,false>(procWindow);
                case 0xf0f0:
                case 0xf0ff:
                    return process<true ,false,true ,false>(procWindow);
                case 0xff00:
                case 0xff0f:
                    return process<true ,true ,false,false>(procWindow);
                case 0xfff0:
                case 0xffff:
                    return process<true ,true ,true ,false>(procWindow);
            }
        } else if (nComponents == 4) {
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
    }

    template<bool processR, bool processG, bool processB, bool processA>
    void process(const OfxRectI& procWindow)
    {
        assert(nComponents == 1 || nComponents == 3 || nComponents == 4);
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
                for (int c = 0; c < 4; ++c) {
                    unpPix[c] = srcPix[c]/(float)maxValue;
                    if (processR && c == 0) {
                        tmpPix[0] = unpPix[0] * (float)_value.r;
                    } else if (processG && c == 1) {
                        tmpPix[1] = unpPix[1] * (float)_value.g;
                    } else if (processB && c == 2) {
                        tmpPix[2] = unpPix[2] * (float)_value.b;
                    } else if (processA && c == 3) {
                        tmpPix[3] = unpPix[3] * (float)_value.a;
                    } else {
                        tmpPix[c] = unpPix[c];
                    }
                    dstPix[c] = tmpPix[c]*maxValue;
                }
                // copy back original values from unprocessed channels
                if (nComponents == 1) {
                    if (!processA) {
                        dstPix[0] = srcPix[0];
                    }
                } else if (nComponents == 3 || nComponents == 4) {
                    if (!processR) {
                        dstPix[0] = srcPix[0];
                    }
                    if (!processG) {
                        dstPix[1] = srcPix[1];
                    }
                    if (!processB) {
                        dstPix[2] = srcPix[2];
                    }
                    if (!processA && nComponents == 4) {
                        dstPix[3] = srcPix[3];
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
class FrameBlendPlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    FrameBlendPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    , _maskClip(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == ePixelComponentRGB || _dstClip->getPixelComponents() == ePixelComponentRGBA));
        _srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(_srcClip && (_srcClip->getPixelComponents() == ePixelComponentRGB || _srcClip->getPixelComponents() == ePixelComponentRGBA));
        _maskClip = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || _maskClip->getPixelComponents() == ePixelComponentAlpha);
        _processR = fetchBooleanParam(kParamProcessR);
        _processG = fetchBooleanParam(kParamProcessG);
        _processB = fetchBooleanParam(kParamProcessB);
        _processA = fetchBooleanParam(kParamProcessA);
        assert(_processR && _processG && _processB && _processA);
        _value = fetchRGBAParam(kParamValueName);
        assert(_value);
        _mix = fetchDoubleParam(kParamMix);
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
    }

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(FrameBlendProcessorBase &, const OFX::RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;
    OFX::Clip *_maskClip;
    OFX::BooleanParam* _processR;
    OFX::BooleanParam* _processG;
    OFX::BooleanParam* _processB;
    OFX::BooleanParam* _processA;
    OFX::RGBAParam *_value;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskInvert;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
FrameBlendPlugin::setupAndProcess(FrameBlendProcessorBase &processor, const OFX::RenderArguments &args)
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
        dst->getField() != args.fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    std::auto_ptr<const OFX::Image> src(_srcClip->fetchImage(args.time));
    if (src.get()) {
        if (src->getRenderScale().x != args.renderScale.x ||
            src->getRenderScale().y != args.renderScale.y ||
            src->getField() != args.fieldToRender) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    std::auto_ptr<OFX::Image> mask(getContext() != OFX::eContextFilter ? _maskClip->fetchImage(args.time) : 0);
    // do we do masking
    if (getContext() != OFX::eContextFilter && _maskClip->isConnected()) {
        if (mask.get()) {
            if (mask->getRenderScale().x != args.renderScale.x ||
                mask->getRenderScale().y != args.renderScale.y ||
                mask->getField() != args.fieldToRender) {
                setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                OFX::throwSuiteStatusException(kOfxStatFailed);
            }
        }
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    // set the images
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());
    // set the render window
    processor.setRenderWindow(args.renderWindow);

    bool processR, processG, processB, processA;
    _processR->getValueAtTime(args.time, processR);
    _processG->getValueAtTime(args.time, processG);
    _processB->getValueAtTime(args.time, processB);
    _processA->getValueAtTime(args.time, processA);
    RGBAValues value;
    _value->getValueAtTime(args.time, value.r, value.g, value.b, value.a);
    double mix;
    _mix->getValueAtTime(args.time, mix);
    processor.setValues(processR, processG, processB, processA,
                        value, mix);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

// the overridden render function
void
FrameBlendPlugin::render(const OFX::RenderArguments &args)
{

    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert(kSupportsMultipleClipPARs   || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth());
    assert(dstComponents == OFX::ePixelComponentAlpha || dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA);
    if (dstComponents == OFX::ePixelComponentRGBA) {
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte: {
                FrameBlendProcessor<unsigned char, 4, 255> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort: {
                FrameBlendProcessor<unsigned short, 4, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat: {
                FrameBlendProcessor<float, 4, 1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else if (dstComponents == OFX::ePixelComponentAlpha) {
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte: {
                FrameBlendProcessor<unsigned char, 1, 255> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort: {
                FrameBlendProcessor<unsigned short, 1, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat: {
                FrameBlendProcessor<float, 1, 1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else {
        assert(dstComponents == OFX::ePixelComponentRGB);
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte: {
                FrameBlendProcessor<unsigned char, 3, 255> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort: {
                FrameBlendProcessor<unsigned short, 3, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat: {
                FrameBlendProcessor<float, 3, 1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}


bool
FrameBlendPlugin::isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &/*identityTime*/)
{
    double mix;
    _mix->getValueAtTime(args.time, mix);

    if (mix == 0. /*|| (!processR && !processG && !processB && !processA)*/) {
        identityClip = _srcClip;
        return true;
    }

    bool processR, processG, processB, processA;
    _processR->getValueAtTime(args.time, processR);
    _processG->getValueAtTime(args.time, processG);
    _processB->getValueAtTime(args.time, processB);
    _processA->getValueAtTime(args.time, processA);
    RGBAValues value;
    _value->getValueAtTime(args.time, value.r, value.g, value.b, value.a);
    if ((!processR || value.r == 1.) &&
        (!processG || value.g == 1.) &&
        (!processB || value.b == 1.) &&
        (!processA || value.a == 1.)) {
        identityClip = _srcClip;
        return true;
    }
    return false;
}

mDeclarePluginFactory(FrameBlendPluginFactory, {}, {});

void FrameBlendPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
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
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
}

void FrameBlendPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
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

    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessR);
        param->setLabel(kParamProcessRLabel);
        param->setHint(kParamProcessRHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine);
        page->addChild(*param);
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessG);
        param->setLabel(kParamProcessGLabel);
        param->setHint(kParamProcessGHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine);
        page->addChild(*param);
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessB);
        param->setLabel(kParamProcessBLabel);
        param->setHint(kParamProcessBHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine);
        page->addChild(*param);
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessA);
        param->setLabel(kParamProcessALabel);
        param->setHint(kParamProcessAHint);
        param->setDefault(false);
        page->addChild(*param);
    }

    {
        RGBAParamDescriptor *param = desc.defineRGBAParam(kParamValueName);
        param->setLabel(kParamValueLabel);
        param->setHint(kParamValueHint);
        param->setDefault(1.0, 1.0, 1.0, 1.0);
        param->setDisplayRange(0, 0, 0, 0, 4, 4, 4, 4);
        param->setAnimates(true); // can animate
        page->addChild(*param);
    }

    ofxsMaskMixDescribeParams(desc, page);
}

OFX::ImageEffect* FrameBlendPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new FrameBlendPlugin(handle);
}

void getFrameBlendPluginID(OFX::PluginFactoryArray &ids)
{
    static FrameBlendPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

