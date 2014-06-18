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
#define kShufflePluginDescription "Rearrange channels from one or two inputs and/or convert to different bit depth or components. No colorspace conversion is done (mapping is linear, even for 8-bit and 16-bit types)."


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
#define kClipInfoParamName "clipInfo"
#define kClipInfoParamLabel "Clip Info..."
#define kClipInfoParamHint "Display information about the inputs"

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


static int nComps(PixelComponentEnum e)
{
    switch(e) {
        case OFX::ePixelComponentRGBA:
            return 4;
        case OFX::ePixelComponentRGB:
            return 3;
        case OFX::ePixelComponentAlpha:
            return 1;
        default:
            return 0;
    }
}

class ShufflerBase : public OFX::ImageProcessor
{
protected:
    OFX::Image *_srcImgA;
    OFX::Image *_srcImgB;
    PixelComponentEnum _outputComponents;
    BitDepthEnum _outputBitDepth;
    int _nComponentsDst;
    InputChannelEnum _channelMap[4];

    public :
    ShufflerBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImgA(0)
    , _srcImgB(0)
    , _outputComponents(ePixelComponentNone)
    , _outputBitDepth(eBitDepthNone)
    , _nComponentsDst(0)
    {
    }

    void setSrcImg(OFX::Image *A, OFX::Image *B) {_srcImgA = A; _srcImgB = B;}

    void setValues(PixelComponentEnum outputComponents,
                   BitDepthEnum outputBitDepth,
                   InputChannelEnum *channelMap)
    {
        _outputComponents = outputComponents,
        _outputBitDepth = outputBitDepth;
        _nComponentsDst = nComps(outputComponents);
        for (int c = 0; c < _nComponentsDst; ++c) {
            _channelMap[c] = channelMap[c];
        }
    }
};

//////////////////////////////
// PIXEL CONVERSION ROUTINES

/// maps 0-(numvals-1) to 0.-1.
template<int numvals>
static float intToFloat(int value)
{
    return value / (float)(numvals-1);
}

/// maps °.-1. to 0-(numvals-1)
template<int numvals>
static int floatToInt(float value)
{
    if (value <= 0) {
        return 0;
    } else if (value >= 1.) {
        return numvals - 1;
    }
    return value * (numvals-1) + 0.5;
}

template <typename SRCPIX,typename DSTPIX>
static DSTPIX convertPixelDepth(SRCPIX pix);

///explicit template instantiations

template <> float convertPixelDepth(unsigned char pix)
{
    return intToFloat<65536>(pix);
}

template <> unsigned short convertPixelDepth(unsigned char pix)
{
    // 0x01 -> 0x0101, 0x02 -> 0x0202, ..., 0xff -> 0xffff
    return (unsigned short)((pix << 8) + pix);
}

template <> unsigned char convertPixelDepth(unsigned char pix)
{
    return pix;
}

template <> unsigned char convertPixelDepth(unsigned short pix)
{
    // the following is from ImageMagick's quantum.h
    return (unsigned char)(((pix+128UL)-((pix+128UL) >> 8)) >> 8);
}

template <> float convertPixelDepth(unsigned short pix)
{
    return intToFloat<65536>(pix);
}

template <> unsigned short convertPixelDepth(unsigned short pix)
{
    return pix;
}

template <> unsigned char convertPixelDepth(float pix)
{
    return (unsigned char)floatToInt<256>(pix);
}

template <> unsigned short convertPixelDepth(float pix)
{
    return (unsigned short)floatToInt<65536>(pix);
}

template <> float convertPixelDepth(float pix)
{
    return pix;
}


template <class PIXSRC, class PIXDST, int nComponentsDst>
class Shuffler : public ShufflerBase
{
    public :
    Shuffler(OFX::ImageEffect &instance)
    : ShufflerBase(instance)
    {
    }

    void multiThreadProcessImages(OfxRectI procWindow)
    {
        OFX::Image* channelMapImg[nComponentsDst];
        int channelMapComp[nComponentsDst]; // channel component, or value if no image
        int srcMapComp[4]; // R,G,B,A components for src
        PixelComponentEnum srcComponents = ePixelComponentNone;
        if (_srcImgA) {
            srcComponents = _srcImgA->getPixelComponents();
        } else if (_srcImgB) {
            srcComponents = _srcImgB->getPixelComponents();
        }
        switch (srcComponents) {
            case OFX::ePixelComponentRGBA:
                srcMapComp[0] = 0;
                srcMapComp[1] = 1;
                srcMapComp[2] = 2;
                srcMapComp[3] = 3;
                break;
            case OFX::ePixelComponentRGB:
                srcMapComp[0] = 0;
                srcMapComp[1] = 1;
                srcMapComp[2] = 2;
                srcMapComp[3] = -1;
                break;
            case OFX::ePixelComponentAlpha:
                srcMapComp[0] = -1;
                srcMapComp[1] = -1;
                srcMapComp[2] = -1;
                srcMapComp[3] = 0;
                break;
            default:
                srcMapComp[0] = -1;
                srcMapComp[1] = -1;
                srcMapComp[2] = -1;
                srcMapComp[3] = -1;
                break;
        }
        for (int c = 0; c < nComponentsDst; ++c) {
            channelMapImg[c] = NULL;
            channelMapComp[c] = 0;
            switch (_channelMap[c]) {
                case eInputChannelAR:
                    if (_srcImgA && srcMapComp[0] >= 0) {
                        channelMapImg[c] = _srcImgA;
                        channelMapComp[c] = srcMapComp[0]; // srcImg may not have R!!!
                    }
                    break;
                case eInputChannelAG:
                    if (_srcImgA && srcMapComp[1] >= 0) {
                        channelMapImg[c] = _srcImgA;
                        channelMapComp[c] = srcMapComp[1];
                    }
                    break;
                case eInputChannelAB:
                    if (_srcImgA && srcMapComp[2] >= 0) {
                        channelMapImg[c] = _srcImgA;
                        channelMapComp[c] = srcMapComp[2];
                    }
                    break;
                case eInputChannelAA:
                    if (_srcImgA && srcMapComp[3] >= 0) {
                        channelMapImg[c] = _srcImgA;
                        channelMapComp[c] = srcMapComp[3];
                    }
                    break;
                case eInputChannel0:
                    channelMapComp[c] = 0;
                    break;
                case eInputChannel1:
                    channelMapComp[c] = 1;
                    break;
                case eInputChannelBR:
                    if (_srcImgB && srcMapComp[0] >= 0) {
                        channelMapImg[c] = _srcImgB;
                        channelMapComp[c] = srcMapComp[0];
                    }
                    break;
                case eInputChannelBG:
                    if (_srcImgB && srcMapComp[1] >= 0) {
                        channelMapImg[c] = _srcImgB;
                        channelMapComp[c] = srcMapComp[1];
                    }
                    break;
                case eInputChannelBB:
                    if (_srcImgB && srcMapComp[2] >= 0) {
                        channelMapImg[c] = _srcImgB;
                        channelMapComp[c] = srcMapComp[2];
                    }
                    break;
                case eInputChannelBA:
                    if (_srcImgB && srcMapComp[3] >= 0) {
                        channelMapImg[c] = _srcImgB;
                        channelMapComp[c] = srcMapComp[3];
                    }
                    break;
            }
        }
        // now compute the transformed image, component by component
        for (int c = 0; c < nComponentsDst; ++c) {
            OFX::Image* srcImg = channelMapImg[c];
            int srcComp = channelMapComp[c];

            for(int y = procWindow.y1; y < procWindow.y2; y++) {
                PIXDST *dstPix = (PIXDST *) _dstImg->getPixelAddress(procWindow.x1, y);
                for(int x = procWindow.x1; x < procWindow.x2; x++) {
                    PIXSRC *srcPix = (PIXSRC *)  (srcImg ? srcImg->getPixelAddress(x, y) : 0);

                    dstPix[c] = srcPix ? convertPixelDepth<PIXSRC,PIXDST>(srcPix[srcComp]) : convertPixelDepth<float,PIXDST>(srcComp);
                    dstPix += nComponentsDst;
                }
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
    , _outputComponents(0)
    , _outputBitDepth(0)
    , _r(0)
    , _g(0)
    , _b(0)
    , _a(0)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        assert(dstClip_ && (dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA || dstClip_->getPixelComponents() == ePixelComponentAlpha));
        srcClipA_ = fetchClip(context == eContextGeneral ? kSourceClipAName : kOfxImageEffectSimpleSourceClipName);
        assert(srcClipA_ && (srcClipA_->getPixelComponents() == ePixelComponentRGB || srcClipA_->getPixelComponents() == ePixelComponentRGBA || srcClipA_->getPixelComponents() == ePixelComponentAlpha));
        if (context == eContextGeneral) {
            srcClipB_ = fetchClip(kSourceClipBName);
            assert(srcClipB_ && (srcClipB_->getPixelComponents() == ePixelComponentRGB || srcClipB_->getPixelComponents() == ePixelComponentRGBA || srcClipB_->getPixelComponents() == ePixelComponentAlpha));
        }
        _outputComponents = fetchChoiceParam(kOutputComponentsParamName);
        if (getImageEffectHostDescription()->supportsMultipleClipDepths) {
            _outputBitDepth = fetchChoiceParam(kOutputBitDepthParamName);
        }
        _r = fetchChoiceParam(kOutputRParamName);
        _g = fetchChoiceParam(kOutputGParamName);
        _b = fetchChoiceParam(kOutputBParamName);
        _a = fetchChoiceParam(kOutputAParamName);
        enableComponents();
    }

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) /* OVERRIDE FINAL */;

    /** @brief get the clip preferences */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) /* OVERRIDE FINAL */;

    /* override changedParam */
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) /* OVERRIDE FINAL */;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) /* OVERRIDE FINAL */;

private:
    void enableComponents(void);

    /* internal render function */
    template <class DSTPIX, int nComponentsDst>
    void renderInternalForDstBitDepth(const OFX::RenderArguments &args, OFX::BitDepthEnum srcBitDepth);

    template <int nComponentsDst>
    void renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum srcBitDepth, OFX::BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(ShufflerBase &, const OFX::RenderArguments &args);

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
    //OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
    std::auto_ptr<OFX::Image> srcA(srcClipA_ ? srcClipA_->fetchImage(args.time) : 0);
    std::auto_ptr<OFX::Image> srcB(srcClipB_ ? srcClipB_->fetchImage(args.time) : 0);
    OFX::BitDepthEnum    srcBitDepth = eBitDepthNone;
    OFX::PixelComponentEnum srcComponents = ePixelComponentNone;
    if (srcA.get()) {
        srcBitDepth      = srcA->getPixelDepth();
        srcComponents = srcA->getPixelComponents();
        assert(srcClipA_->getPixelComponents() == srcComponents);
    }

    if (srcB.get()) {
        OFX::BitDepthEnum    srcBBitDepth      = srcB->getPixelDepth();
        OFX::PixelComponentEnum srcBComponents = srcB->getPixelComponents();
        assert(srcClipB_->getPixelComponents() == srcBComponents);
        // both input must have the same bit depth and components
        if ((srcBitDepth != eBitDepthNone && srcBitDepth != srcBBitDepth) ||
            (srcComponents != ePixelComponentNone && srcComponents != srcBComponents)) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    int outputComponents_i;
    _outputComponents->getValue(outputComponents_i);
    PixelComponentEnum outputComponents = gOutputComponentsMap[outputComponents_i];
    BitDepthEnum outputBitDepth = srcBitDepth;
    if (getImageEffectHostDescription()->supportsMultipleClipDepths) {
        int outputBitDepth_i;
        _outputBitDepth->getValue(outputBitDepth_i);
        outputBitDepth = gOutputBitDepthMap[outputBitDepth_i];
    }
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

    // compute the components mapping tables
    InputChannelEnum channelMap[4];
    switch(dstComponents) {
        case OFX::ePixelComponentRGBA:
            channelMap[0] = r;
            channelMap[1] = g;
            channelMap[2] = b;
            channelMap[3] = a;
            break;
        case OFX::ePixelComponentRGB:
            channelMap[0] = r;
            channelMap[1] = g;
            channelMap[2] = b;
            channelMap[3] = eInputChannel0;
            break;
        case OFX::ePixelComponentAlpha:
            channelMap[0] = a;
            channelMap[1] = eInputChannel0;
            channelMap[2] = eInputChannel0;
            channelMap[3] = eInputChannel0;
            break;
        default:
            channelMap[0] = eInputChannel0;
            channelMap[1] = eInputChannel0;
            channelMap[2] = eInputChannel0;
            channelMap[3] = eInputChannel0;
            break;
    }
    processor.setValues(outputComponents, outputBitDepth, channelMap);
    processor.setDstImg(dst.get());
    processor.setSrcImg(srcA.get(),srcB.get());
    processor.setRenderWindow(args.renderWindow);

    processor.process();
}

template <class DSTPIX, int nComponentsDst>
void
ShufflePlugin::renderInternalForDstBitDepth(const OFX::RenderArguments &args, OFX::BitDepthEnum srcBitDepth)
{
    switch(srcBitDepth) {
        case OFX::eBitDepthUByte : {
            Shuffler<unsigned char, DSTPIX, nComponentsDst> fred(*this);
            setupAndProcess(fred, args);
        }
            break;
        case OFX::eBitDepthUShort : {
            Shuffler<unsigned short, DSTPIX, nComponentsDst> fred(*this);
            setupAndProcess(fred, args);
        }
            break;
        case OFX::eBitDepthFloat : {
            Shuffler<float, DSTPIX, nComponentsDst> fred(*this);
            setupAndProcess(fred, args);
        }
            break;
        default :
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the internal render function
template <int nComponentsDst>
void
ShufflePlugin::renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum srcBitDepth, OFX::BitDepthEnum dstBitDepth)
{
    switch(dstBitDepth) {
        case OFX::eBitDepthUByte :
            renderInternalForDstBitDepth<unsigned char, nComponentsDst>(args, srcBitDepth);
            break;
        case OFX::eBitDepthUShort :
            renderInternalForDstBitDepth<unsigned short, nComponentsDst>(args, srcBitDepth);
            break;
        case OFX::eBitDepthFloat :
            renderInternalForDstBitDepth<float, nComponentsDst>(args, srcBitDepth);
            break;
        default :
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
ShufflePlugin::render(const OFX::RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();
    assert(dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA || dstComponents == ePixelComponentAlpha);

    // get the components of dstClip_
    int outputComponents_i;
    _outputComponents->getValue(outputComponents_i);
    PixelComponentEnum outputComponents = gOutputComponentsMap[outputComponents_i];
    if (dstComponents != outputComponents) {
        setPersistentMessage(OFX::Message::eMessageError, "", "Shuffle: OFX Host dit not take into account output components");
        OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
    }

    if (getImageEffectHostDescription()->supportsMultipleClipDepths) {
        // get the bitDepth of dstClip_
        int outputBitDepth_i;
        _outputBitDepth->getValue(outputBitDepth_i);
        BitDepthEnum outputBitDepth = gOutputBitDepthMap[outputBitDepth_i];
        if (dstBitDepth != outputBitDepth) {
            setPersistentMessage(OFX::Message::eMessageError, "", "Shuffle: OFX Host dit not take into account output bit depth");
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    OFX::BitDepthEnum srcBitDepth = srcClipA_->getPixelDepth();

    if (srcClipB_ && srcClipA_->isConnected() && srcClipB_->isConnected()) {
        OFX::BitDepthEnum srcBBitDepth = srcClipB_->getPixelDepth();
        // both input must have the same bit depth
        if (srcBitDepth != srcBBitDepth) {
            setPersistentMessage(OFX::Message::eMessageError, "", "Shuffle: both inputs must have the same bit depth");
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    if (dstComponents == OFX::ePixelComponentRGBA) {
            renderInternal<4>(args, srcBitDepth, dstBitDepth);
    } else if (dstComponents == OFX::ePixelComponentRGB) {
            renderInternal<3>(args, srcBitDepth, dstBitDepth);
    } else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
            renderInternal<1>(args, srcBitDepth, dstBitDepth);
    }
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

    if (getImageEffectHostDescription()->supportsMultipleClipDepths) {
        // set the bitDepth of dstClip_
        int outputBitDepth_i;
        _outputBitDepth->getValue(outputBitDepth_i);
        BitDepthEnum outputBitDepth = gOutputBitDepthMap[outputBitDepth_i];
        clipPreferences.setClipBitDepth(*dstClip_, outputBitDepth);
    }
}

static std::string
imageFormatString(PixelComponentEnum components, BitDepthEnum bitDepth)
{
    std::string s;
    switch (components) {
        case OFX::ePixelComponentRGBA:
            s += "RGBA";
            break;
        case OFX::ePixelComponentRGB:
            s += "RGB";
            break;
        case OFX::ePixelComponentAlpha:
            s += "Alpha";
            break;
        case OFX::ePixelComponentCustom:
            s += "Custom";
            break;
        case OFX::ePixelComponentNone:
            s += "None";
            break;
        default:
            s += "[unknown components]";
            break;
    }
    switch (bitDepth) {
        case OFX::eBitDepthUByte:
            s += "8u";
            break;
        case OFX::eBitDepthUShort:
            s += "16u";
            break;
        case OFX::eBitDepthFloat:
            s += "32f";
            break;
        case OFX::eBitDepthCustom:
            s += "x";
            break;
        case OFX::ePixelComponentNone:
            s += "0";
            break;
        default:
            s += "[unknown bit depth]";
            break;
    }
    return s;
}

void
ShufflePlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kOutputComponentsParamName) {
        enableComponents();
    } else if (paramName == kClipInfoParamName) {
        std::string msg;
        msg += "Input A: ";
        if (!srcClipA_) {
            msg += "N/A";
        } else {
            msg += imageFormatString(srcClipA_->getPixelComponents(), srcClipA_->getPixelDepth());
        }
        msg += "\n";
        if (getContext() == eContextGeneral) {
            msg += "Input B: ";
            if (!srcClipB_) {
                msg += "N/A";
            } else {
                msg += imageFormatString(srcClipB_->getPixelComponents(), srcClipB_->getPixelDepth());
            }
            msg += "\n";
        }
        msg += "Output: ";
        if (!dstClip_) {
            msg += "N/A";
        } else {
            msg += imageFormatString(dstClip_->getPixelComponents(), dstClip_->getPixelDepth());
        }
        msg += "\n";
        sendMessage(OFX::Message::eMessageMessage, "", msg);
    }
}

void
ShufflePlugin::changedClip(const InstanceChangedArgs &args, const std::string &clipName)
{
    if (getContext() == eContextGeneral &&
        (clipName == kSourceClipAName || clipName == kSourceClipBName)) {
        // check that A and B are compatible if they're both connected
        OFX::BitDepthEnum srcBitDepth = srcClipA_->getPixelDepth();

        if (srcClipB_ && srcClipA_->isConnected() && srcClipB_->isConnected()) {
            OFX::BitDepthEnum srcBBitDepth = srcClipB_->getPixelDepth();
            // both input must have the same bit depth
            if (srcBitDepth != srcBBitDepth) {
                setPersistentMessage(OFX::Message::eMessageError, "", "Shuffle: both inputs must have the same bit depth");
                OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
            }
        }
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


mDeclarePluginFactory(ShufflePluginFactory, {}, {});

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
    desc.addClipPreferencesSlaveParam(*outputComponents);

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
        desc.addClipPreferencesSlaveParam(*outputBitDepth);
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

    PushButtonParamDescriptor *clipInfo = desc.definePushButtonParam(kClipInfoParamName);
    clipInfo->setLabels(kClipInfoParamLabel, kClipInfoParamLabel, kClipInfoParamLabel);
    clipInfo->setHint(kClipInfoParamHint);
    page->addChild(*clipInfo);
}

OFX::ImageEffect* ShufflePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new ShufflePlugin(handle, context);
}

void getShufflePluginID(OFX::PluginFactoryArray &ids)
{
    static ShufflePluginFactory p("net.sf.openfx:ShufflePlugin", /*pluginVersionMajor=*/1, /*pluginVersionMinor=*/0);
    ids.push_back(&p);
}

