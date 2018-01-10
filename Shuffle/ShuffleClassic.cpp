/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2018 INRIA
 *
 * openfx-misc is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * openfx-misc is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with openfx-misc.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

/*
 * OFX Shuffle plugin.
 */

#include <cmath>

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxsCoords.h"
#include "ofxsThreadSuite.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "ShuffleOFX"
#define kPluginGrouping "Channel"
#define kPluginDescription "Rearrange channels from one or two inputs and/or convert to different bit depth or components. No colorspace conversion is done (mapping is linear, even for 8-bit and 16-bit types)."
#define kPluginIdentifier "net.sf.openfx.ShufflePlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths true // can convert depth
#define kRenderThreadSafety eRenderFullySafe

#define kParamOutputComponents "outputComponents"
#define kParamOutputComponentsLabel "Output Components"
#define kParamOutputComponentsHint "Select what types of components the plug-in should output, this has an effect only when the Output Layer is set to the Color layer." \
    " This controls what should be the components for the Color Layer: Alpha, RGB or RGBA"
#define kParamOutputComponentsOptionRGBA "RGBA", "Output RGBA components.", "rgba"
#define kParamOutputComponentsOptionRGB "RGB", "Output RGB components.", "rgb"
#define kParamOutputComponentsOptionAlpha "Alpha", "Output Alpha component.", "alpha"

#define kParamOutputBitDepth "outputBitDepth"
#define kParamOutputBitDepthLabel "Output Bit Depth"
#define kParamOutputBitDepthHint "Bit depth of the output.\nWARNING: the conversion is linear, even for 8-bit or 16-bit depth. Use with care."
#define kParamOutputBitDepthOptionByte "Byte (8 bits)", "Output 8-bit images.", "byte"
#define kParamOutputBitDepthOptionShort "Short (16 bits)", "Output 16-bit images.", "short"
#define kParamOutputBitDepthOptionFloat "Float (32 bits)", "Output 32-bit floating-point images.", "float"

#define kParamOutputPremultiplication "outputPremult"
#define kParamOutputPremultiplicationLabel "Output Premult"
#define kParamOutputPremultiplicationHint "Set the premultiplication metadata on the output. This does not modify the data itself. The premultiplication metadata will flow downstream so that further down effects " \
    "know what kind of data to expect. By default it should be set to Unpremultiplied and you should always provide the Shuffle node " \
    "unpremultiplied data. Providing alpha-premultiplied data in input of the Shuffle may produce wrong results because of the potential loss " \
    "of the associated alpha channel."


#define kParamOutputR "outputR"
#define kParamOutputRLabel "R"
#define kParamOutputRHint "Input channel for the output red channel"

#define kParamOutputG "outputG"
#define kParamOutputGLabel "G"
#define kParamOutputGHint "Input channel for the output green channel"

#define kParamOutputB "outputB"
#define kParamOutputBLabel "B"
#define kParamOutputBHint "Input channel for the output blue channel"

#define kParamOutputA "outputA"
#define kParamOutputALabel "A"
#define kParamOutputAHint "Input channel for the output alpha channel"

#define kParamOutputOptionAR "A.r", "R channel from input A", "ar"
#define kParamOutputOptionAG "A.g", "G channel from input A", "ag"
#define kParamOutputOptionAB "A.b", "B channel from input A", "ab"
#define kParamOutputOptionAA "A.a", "A channel from input A", "aa"
#define kParamOutputOption0 "0", "0 constant channel", "zero"
#define kParamOutputOption1 "1", "1 constant channel", "one"
#define kParamOutputOptionBR "B.r", "R channel from input B", "br"
#define kParamOutputOptionBG "B.g", "G channel from input B", "bg"
#define kParamOutputOptionBB "B.b", "B channel from input B", "bb"
#define kParamOutputOptionBA "B.a", "A channel from input B", "ba"

#define kParamClipInfo "clipInfo"
#define kParamClipInfoLabel "Clip Info..."
#define kParamClipInfoHint "Display information about the inputs"

// TODO: sRGB/Rec.709 conversions for byte/short types

enum InputChannelEnum
{
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

#define kClipA "A"
#define kClipB "B"

static bool gSupportsBytes  = false;
static bool gSupportsShorts = false;
static bool gSupportsFloats = false;
static bool gSupportsRGBA   = false;
static bool gSupportsRGB    = false;
static bool gSupportsAlpha  = false;
static PixelComponentEnum gOutputComponentsMap[4];
static BitDepthEnum gOutputBitDepthMap[4];

using namespace OFX;


static int
nComps(PixelComponentEnum e)
{
    switch (e) {
    case ePixelComponentRGBA:

        return 4;
    case ePixelComponentRGB:

        return 3;
    case ePixelComponentXY:

        return 2;
    case ePixelComponentAlpha:

        return 1;
    default:

        return 0;
    }
}

class ShufflerBase
    : public ImageProcessor
{
protected:
    const Image *_srcImgA;
    const Image *_srcImgB;
    PixelComponentEnum _outputComponents;
    int _outputComponentCount;
    BitDepthEnum _outputBitDepth;
    std::vector<InputChannelEnum> _channelMap;

public:
    ShufflerBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _srcImgA(NULL)
        , _srcImgB(NULL)
        , _outputComponents(ePixelComponentNone)
        , _outputComponentCount(0)
        , _outputBitDepth(eBitDepthNone)
        , _channelMap()
    {
    }

    void setSrcImg(const Image *A,
                   const Image *B) {_srcImgA = A; _srcImgB = B; }

    void setValues(PixelComponentEnum outputComponents,
                   BitDepthEnum outputBitDepth,
                   const std::vector<InputChannelEnum> &channelMap)
    {
        _outputComponents = outputComponents,
        _outputComponentCount = nComps(outputComponents);
        _outputBitDepth = outputBitDepth;
        assert( _outputComponentCount == (int)channelMap.size() );
        _channelMap = channelMap;
    }
};

//////////////////////////////
// PIXEL CONVERSION ROUTINES

/// maps 0-(numvals-1) to 0.-1.
template<int numvals>
static float
intToFloat(int value)
{
    return value / (float)(numvals - 1);
}

/// maps °.-1. to 0-(numvals-1)
template<int numvals>
static int
floatToInt(float value)
{
    if (value <= 0) {
        return 0;
    } else if (value >= 1.) {
        return numvals - 1;
    }

    return value * (numvals - 1) + 0.5f;
}

template <typename SRCPIX, typename DSTPIX>
static DSTPIX convertPixelDepth(SRCPIX pix);

///explicit template instantiations

template <>
float
convertPixelDepth(unsigned char pix)
{
    return intToFloat<65536>(pix);
}

template <>
unsigned short
convertPixelDepth(unsigned char pix)
{
    // 0x01 -> 0x0101, 0x02 -> 0x0202, ..., 0xff -> 0xffff
    return (unsigned short)( (pix << 8) + pix );
}

template <>
unsigned char
convertPixelDepth(unsigned char pix)
{
    return pix;
}

template <>
unsigned char
convertPixelDepth(unsigned short pix)
{
    // the following is from ImageMagick's quantum.h
    return (unsigned char)( ( (pix + 128UL) - ( (pix + 128UL) >> 8 ) ) >> 8 );
}

template <>
float
convertPixelDepth(unsigned short pix)
{
    return intToFloat<65536>(pix);
}

template <>
unsigned short
convertPixelDepth(unsigned short pix)
{
    return pix;
}

template <>
unsigned char
convertPixelDepth(float pix)
{
    return (unsigned char)floatToInt<256>(pix);
}

template <>
unsigned short
convertPixelDepth(float pix)
{
    return (unsigned short)floatToInt<65536>(pix);
}

template <>
float
convertPixelDepth(float pix)
{
    return pix;
}

template <class PIXSRC, class PIXDST, int nComponentsDst>
class Shuffler
    : public ShufflerBase
{
public:
    Shuffler(ImageEffect &instance)
        : ShufflerBase(instance)
    {
    }

private:
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        const Image* channelMapImg[nComponentsDst];
        int channelMapComp[nComponentsDst]; // channel component, or value if no image
        int srcMapComp[4]; // R,G,B,A components for src
        PixelComponentEnum srcComponents = ePixelComponentNone;

        if (_srcImgA) {
            srcComponents = _srcImgA->getPixelComponents();
        } else if (_srcImgB) {
            srcComponents = _srcImgB->getPixelComponents();
        }
        switch (srcComponents) {
        case ePixelComponentRGBA:
            srcMapComp[0] = 0;
            srcMapComp[1] = 1;
            srcMapComp[2] = 2;
            srcMapComp[3] = 3;
            break;
        case ePixelComponentRGB:
            srcMapComp[0] = 0;
            srcMapComp[1] = 1;
            srcMapComp[2] = 2;
            srcMapComp[3] = -1;
            break;
        case ePixelComponentAlpha:
            srcMapComp[0] = -1;
            srcMapComp[1] = -1;
            srcMapComp[2] = -1;
            srcMapComp[3] = 0;
            break;
#ifdef OFX_EXTENSIONS_NATRON
        case ePixelComponentXY:
            srcMapComp[0] = 0;
            srcMapComp[1] = 1;
            srcMapComp[2] = -1;
            srcMapComp[3] = -1;
            break;
#endif
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
                if ( _srcImgA && ( srcMapComp[0] >= 0) ) {
                    channelMapImg[c] = _srcImgA;
                    channelMapComp[c] = srcMapComp[0];     // srcImg may not have R!!!
                }
                break;
            case eInputChannelAG:
                if ( _srcImgA && ( srcMapComp[1] >= 0) ) {
                    channelMapImg[c] = _srcImgA;
                    channelMapComp[c] = srcMapComp[1];
                }
                break;
            case eInputChannelAB:
                if ( _srcImgA && ( srcMapComp[2] >= 0) ) {
                    channelMapImg[c] = _srcImgA;
                    channelMapComp[c] = srcMapComp[2];
                }
                break;
            case eInputChannelAA:
                if ( _srcImgA && ( srcMapComp[3] >= 0) ) {
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
                if ( _srcImgB && ( srcMapComp[0] >= 0) ) {
                    channelMapImg[c] = _srcImgB;
                    channelMapComp[c] = srcMapComp[0];
                }
                break;
            case eInputChannelBG:
                if ( _srcImgB && ( srcMapComp[1] >= 0) ) {
                    channelMapImg[c] = _srcImgB;
                    channelMapComp[c] = srcMapComp[1];
                }
                break;
            case eInputChannelBB:
                if ( _srcImgB && ( srcMapComp[2] >= 0) ) {
                    channelMapImg[c] = _srcImgB;
                    channelMapComp[c] = srcMapComp[2];
                }
                break;
            case eInputChannelBA:
                if ( _srcImgB && ( srcMapComp[3] >= 0) ) {
                    channelMapImg[c] = _srcImgB;
                    channelMapComp[c] = srcMapComp[3];
                }
                break;
            } // switch
        }
        // now compute the transformed image, component by component
        for (int c = 0; c < nComponentsDst; ++c) {
            const Image* srcImg = channelMapImg[c];
            int srcComp = channelMapComp[c];

            for (int y = procWindow.y1; y < procWindow.y2; y++) {
                if ( _effect.abort() ) {
                    break;
                }

                PIXDST *dstPix = (PIXDST *) _dstImg->getPixelAddress(procWindow.x1, y);

                for (int x = procWindow.x1; x < procWindow.x2; x++) {
                    PIXSRC *srcPix = (PIXSRC *)  (srcImg ? srcImg->getPixelAddress(x, y) : 0);
                    // if there is a srcImg but we are outside of its RoD, it should be considered black and transparent
                    dstPix[c] = srcImg ? convertPixelDepth<PIXSRC, PIXDST>(srcPix ? srcPix[srcComp] : 0) : convertPixelDepth<float, PIXDST>(srcComp);
                    dstPix += nComponentsDst;
                }
            }
        }
    } // multiThreadProcessImages
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ShufflePlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    ShufflePlugin(OfxImageEffectHandle handle,
                  ContextEnum context)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClipA(NULL)
        , _srcClipB(NULL)
        , _outputBitDepth(NULL)
        , _channelParam()
        , _outputComponents(NULL)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (1 <= _dstClip->getPixelComponentCount() && _dstClip->getPixelComponentCount() <= 4) );
        _srcClipA = fetchClip(context == eContextGeneral ? kClipA : kOfxImageEffectSimpleSourceClipName);
        assert( _srcClipA && (1 <= _srcClipA->getPixelComponentCount() && _srcClipA->getPixelComponentCount() <= 4) );
        if (context == eContextGeneral) {
            _srcClipB = fetchClip(kClipB);
            assert( _srcClipB && (1 <= _srcClipB->getPixelComponentCount() && _srcClipB->getPixelComponentCount() <= 4) );
        }
        if (getImageEffectHostDescription()->supportsMultipleClipDepths) {
            _outputBitDepth = fetchChoiceParam(kParamOutputBitDepth);
        }
        _channelParam[0] = fetchChoiceParam(kParamOutputR);
        _channelParam[1] = fetchChoiceParam(kParamOutputG);
        _channelParam[2] = fetchChoiceParam(kParamOutputB);
        _channelParam[3] = fetchChoiceParam(kParamOutputA);

        _outputComponents = fetchChoiceParam(kParamOutputComponents);

        _outputPremult = fetchChoiceParam(kParamOutputPremultiplication);

        updateVisibility();
    }

private:

    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /* override is identity */
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /** @brief get the clip preferences */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /* override changedParam */
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

private:
    void enableComponents(void);

    /* internal render function */
    template <class DSTPIX, int nComponentsDst>
    void renderInternalForDstBitDepth(const RenderArguments &args, BitDepthEnum srcBitDepth);

    template <int nComponentsDst>
    void renderInternal(const RenderArguments &args, BitDepthEnum srcBitDepth, BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(ShufflerBase &, const RenderArguments &args);

    void updateVisibility();

    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClipA;
    Clip *_srcClipB;
    ChoiceParam *_outputBitDepth;
    ChoiceParam* _channelParam[4];
    ChoiceParam *_outputComponents;
    ChoiceParam *_outputPremult;
};

bool
ShufflePlugin::isIdentity(const IsIdentityArguments &args,
                          Clip * &identityClip,
                          double & /*identityTime*/
                          , int& /*view*/, std::string& /*plane*/)
{
    const double time = args.time;
    PixelComponentEnum srcAComponents = _srcClipA ? _srcClipA->getPixelComponents() : ePixelComponentNone;
    PixelComponentEnum srcBComponents = _srcClipB ? _srcClipB->getPixelComponents() : ePixelComponentNone;
    PixelComponentEnum dstComponents = _dstClip ? _dstClip->getPixelComponents() : ePixelComponentNone;

    {
        InputChannelEnum r = InputChannelEnum( _channelParam[0]->getValueAtTime(time) );
        InputChannelEnum g = InputChannelEnum( _channelParam[1]->getValueAtTime(time) );
        InputChannelEnum b = InputChannelEnum( _channelParam[2]->getValueAtTime(time) );
        InputChannelEnum a = InputChannelEnum( _channelParam[3]->getValueAtTime(time) );

        if ( (r == eInputChannelAR) && (g == eInputChannelAG) && (b == eInputChannelAB) && (a == eInputChannelAA) && _srcClipA && (srcAComponents == dstComponents) ) {
            identityClip = _srcClipA;

            return true;
        }
        if ( (r == eInputChannelBR) && (g == eInputChannelBG) && (b == eInputChannelBB) && (a == eInputChannelBA) && _srcClipB && (srcBComponents == dstComponents) ) {
            identityClip = _srcClipB;

            return true;
        }
    }

    return false;
}

bool
ShufflePlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                     OfxRectD &rod)
{
    const double time = args.time;
    InputChannelEnum r = InputChannelEnum( _channelParam[0]->getValueAtTime(time) );
    InputChannelEnum g = InputChannelEnum( _channelParam[1]->getValueAtTime(time) );
    InputChannelEnum b = InputChannelEnum( _channelParam[2]->getValueAtTime(time) );
    InputChannelEnum a = InputChannelEnum( _channelParam[3]->getValueAtTime(time) );

    if ( (r == eInputChannelAR) && (g == eInputChannelAG) && (b == eInputChannelAB) && (a == eInputChannelAA) && _srcClipA ) {
        rod = _srcClipA->getRegionOfDefinition(args.time);

        return true;
    }
    if ( (r == eInputChannelBR) && (g == eInputChannelBG) && (b == eInputChannelBB) && (a == eInputChannelBA) && _srcClipB ) {
        rod = _srcClipB->getRegionOfDefinition(args.time);

        return true;
    }
    if ( _srcClipA && _srcClipA->isConnected() && _srcClipB && _srcClipB->isConnected() ) {
        OfxRectD rodA = _srcClipA->getRegionOfDefinition(args.time);
        OfxRectD rodB = _srcClipB->getRegionOfDefinition(args.time);
        Coords::rectBoundingBox(rodA, rodB, &rod);

        return true;
    }

    return false;
}

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */


/* set up and run a processor */
void
ShufflePlugin::setupAndProcess(ShufflerBase &processor,
                               const RenderArguments &args)
{
    auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );

    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    const double time = args.time;
    BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        throwSuiteStatusException(kOfxStatFailed);
    }
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);
    }


    InputChannelEnum r, g, b, a;
    // compute the components mapping tables
    std::vector<InputChannelEnum> channelMap;
    auto_ptr<const Image> srcA( ( _srcClipA && _srcClipA->isConnected() ) ?
                                     _srcClipA->fetchImage(args.time) : 0 );
    auto_ptr<const Image> srcB( ( _srcClipB && _srcClipB->isConnected() ) ?
                                     _srcClipB->fetchImage(args.time) : 0 );
    BitDepthEnum srcBitDepth = eBitDepthNone;
    PixelComponentEnum srcComponents = ePixelComponentNone;
    if ( srcA.get() ) {
        if ( (srcA->getRenderScale().x != args.renderScale.x) ||
             ( srcA->getRenderScale().y != args.renderScale.y) ||
             ( ( srcA->getField() != eFieldNone) /* for DaVinci Resolve */ && ( srcA->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
        srcBitDepth      = srcA->getPixelDepth();
        srcComponents = srcA->getPixelComponents();
        assert(_srcClipA->getPixelComponents() == srcComponents);
    }

    if ( srcB.get() ) {
        if ( (srcB->getRenderScale().x != args.renderScale.x) ||
             ( srcB->getRenderScale().y != args.renderScale.y) ||
             ( ( srcB->getField() != eFieldNone) /* for DaVinci Resolve */ && ( srcB->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
        BitDepthEnum srcBBitDepth      = srcB->getPixelDepth();
        PixelComponentEnum srcBComponents = srcB->getPixelComponents();
        assert(_srcClipB->getPixelComponents() == srcBComponents);
        // both input must have the same bit depth and components
        if ( ( (srcBitDepth != eBitDepthNone) && (srcBitDepth != srcBBitDepth) ) ||
             ( ( srcComponents != ePixelComponentNone) && ( srcComponents != srcBComponents) ) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    r = (InputChannelEnum)_channelParam[0]->getValueAtTime(time);
    g = (InputChannelEnum)_channelParam[1]->getValueAtTime(time);
    b = (InputChannelEnum)_channelParam[2]->getValueAtTime(time);
    a = (InputChannelEnum)_channelParam[3]->getValueAtTime(time);


    switch (dstComponents) {
    case ePixelComponentRGBA:
        channelMap.resize(4);
        channelMap[0] = r;
        channelMap[1] = g;
        channelMap[2] = b;
        channelMap[3] = a;
        break;
    case ePixelComponentXY:
        channelMap.resize(2);
        channelMap[0] = r;
        channelMap[1] = g;
        break;
    case ePixelComponentRGB:
        channelMap.resize(3);
        channelMap[0] = r;
        channelMap[1] = g;
        channelMap[2] = b;
        break;
    case ePixelComponentAlpha:
        channelMap.resize(1);
        channelMap[0] = a;
        break;
    default:
        channelMap.resize(0);
        break;
    }
    processor.setSrcImg( srcA.get(), srcB.get() );

    PixelComponentEnum outputComponents = gOutputComponentsMap[_outputComponents->getValueAtTime(time)];
    assert(dstComponents == outputComponents);
    BitDepthEnum outputBitDepth = srcBitDepth;
    if (getImageEffectHostDescription()->supportsMultipleClipDepths) {
        outputBitDepth = gOutputBitDepthMap[_outputBitDepth->getValueAtTime(time)];
    }
    assert(outputBitDepth == dstBitDepth);
    processor.setValues(outputComponents, outputBitDepth, channelMap);
    processor.setDstImg( dst.get() );
    processor.setRenderWindow(args.renderWindow);

    processor.process();
} // ShufflePlugin::setupAndProcess

template <class DSTPIX, int nComponentsDst>
void
ShufflePlugin::renderInternalForDstBitDepth(const RenderArguments &args,
                                            BitDepthEnum srcBitDepth)
{
    {
        switch (srcBitDepth) {
        case eBitDepthUByte: {
            Shuffler<unsigned char, DSTPIX, nComponentsDst> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthUShort: {
            Shuffler<unsigned short, DSTPIX, nComponentsDst> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthFloat: {
            Shuffler<float, DSTPIX, nComponentsDst> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        default:
            throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}

// the internal render function
template <int nComponentsDst>
void
ShufflePlugin::renderInternal(const RenderArguments &args,
                              BitDepthEnum srcBitDepth,
                              BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case eBitDepthUByte:
        renderInternalForDstBitDepth<unsigned char, nComponentsDst>(args, srcBitDepth);
        break;
    case eBitDepthUShort:
        renderInternalForDstBitDepth<unsigned short, nComponentsDst>(args, srcBitDepth);
        break;
    case eBitDepthFloat:
        renderInternalForDstBitDepth<float, nComponentsDst>(args, srcBitDepth);
        break;
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
ShufflePlugin::render(const RenderArguments &args)
{
    assert (_srcClipA && _srcClipB && _dstClip);
    if (!_srcClipA || !_srcClipB || !_dstClip) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    const double time = args.time;
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();
    int dstComponentCount  = _dstClip->getPixelComponentCount();
    assert(1 <= dstComponentCount && dstComponentCount <= 4);

    assert( kSupportsMultipleClipPARs   || _srcClipA->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || _srcClipA->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert( kSupportsMultipleClipPARs   || _srcClipB->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || _srcClipB->getPixelDepth()       == _dstClip->getPixelDepth() );
    // get the components of _dstClip

    {
        PixelComponentEnum outputComponents = gOutputComponentsMap[_outputComponents->getValueAtTime(time)];
        if (dstComponents != outputComponents) {
            setPersistentMessage(Message::eMessageError, "", "Shuffle: OFX Host did not take into account output components");
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    if (getImageEffectHostDescription()->supportsMultipleClipDepths) {
        // get the bitDepth of _dstClip
        BitDepthEnum outputBitDepth = gOutputBitDepthMap[_outputBitDepth->getValueAtTime(time)];
        if (dstBitDepth != outputBitDepth) {
            setPersistentMessage(Message::eMessageError, "", "Shuffle: OFX Host did not take into account output bit depth");
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    BitDepthEnum srcBitDepth = _srcClipA->getPixelDepth();

    if ( _srcClipA->isConnected() && _srcClipB->isConnected() ) {
        BitDepthEnum srcBBitDepth = _srcClipB->getPixelDepth();
        // both input must have the same bit depth
        if (srcBitDepth != srcBBitDepth) {
            setPersistentMessage(Message::eMessageError, "", "Shuffle: both inputs must have the same bit depth");
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    switch (dstComponentCount) {
    case 4:
        renderInternal<4>(args, srcBitDepth, dstBitDepth);
        break;
    case 3:
        renderInternal<3>(args, srcBitDepth, dstBitDepth);
        break;
    case 2:
        renderInternal<2>(args, srcBitDepth, dstBitDepth);
        break;
    case 1:
        renderInternal<1>(args, srcBitDepth, dstBitDepth);
        break;
    }
} // ShufflePlugin::render

void
ShufflePlugin::updateVisibility()
{
    PixelComponentEnum dstPixelComps = gOutputComponentsMap[_outputComponents->getValue()];

    // Premult is only needed for RGBA
    _outputPremult->setIsSecretAndDisabled( !(dstPixelComps == ePixelComponentRGBA) );
}

/* Override the clip preferences */
void
ShufflePlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    // set the components of _dstClip
    PixelComponentEnum dstPixelComps = gOutputComponentsMap[_outputComponents->getValue()];

    clipPreferences.setClipComponents(*_dstClip, dstPixelComps);


    //Enable components according to the new dstPixelComps
    enableComponents();

    if (getImageEffectHostDescription()->supportsMultipleClipDepths) {
        // set the bitDepth of _dstClip
        BitDepthEnum outputBitDepth = gOutputBitDepthMap[_outputBitDepth->getValue()];
        clipPreferences.setClipBitDepth(*_dstClip, outputBitDepth);
    }

    PreMultiplicationEnum premult = eImageUnPreMultiplied; // default for Alpha and others
    if (dstPixelComps == ePixelComponentRGB) {
        premult = eImageOpaque;
    } else {
        premult = (PreMultiplicationEnum)_outputPremult->getValue();
    }
    clipPreferences.setOutputPremultiplication(premult);
} // ShufflePlugin::getClipPreferences

static std::string
imageFormatString(PixelComponentEnum components,
                  BitDepthEnum bitDepth)
{
    std::string s;

    switch (components) {
    case ePixelComponentRGBA:
        s += "RGBA";
        break;
    case ePixelComponentRGB:
        s += "RGB";
        break;
    case ePixelComponentAlpha:
        s += "Alpha";
        break;
#ifdef OFX_EXTENSIONS_NUKE
    case ePixelComponentMotionVectors:
        s += "MotionVectors";
        break;
    case ePixelComponentStereoDisparity:
        s += "StereoDisparity";
        break;
#endif
#ifdef OFX_EXTENSIONS_NATRON
    case ePixelComponentXY:
        s += "XY";
        break;
#endif
    case ePixelComponentCustom:
        s += "Custom";
        break;
    case ePixelComponentNone:
        s += "None";
        break;
    default:
        s += "[unknown components]";
        break;
    }
    switch (bitDepth) {
    case eBitDepthUByte:
        s += "8u";
        break;
    case eBitDepthUShort:
        s += "16u";
        break;
    case eBitDepthFloat:
        s += "32f";
        break;
    case eBitDepthCustom:
        s += "x";
        break;
    case eBitDepthNone:
        s += "0";
        break;
#ifdef OFX_EXTENSIONS_VEGAS
    case eBitDepthUByteBGRA:
        s += "8uBGRA";
        break;
    case eBitDepthUShortBGRA:
        s += "16uBGRA";
        break;
    case eBitDepthFloatBGRA:
        s += "32fBGRA";
        break;
#endif
    default:
        s += "[unknown bit depth]";
        break;
    }

    return s;
} // imageFormatString

void
ShufflePlugin::changedParam(const InstanceChangedArgs &args,
                            const std::string &paramName)
{
    //Commented out as it cannot be done here: enableComponents() relies on the clip components but the clip
    //components might not yet be set if the user changed a clip pref slaved param. Instead we have to move it into the getClipPreferences
    /*if (paramName == kParamOutputComponents || paramName == kParamOutputChannels) {
        enableComponents();
       } else*/
    if ( (paramName == kParamClipInfo) && (args.reason == eChangeUserEdit) ) {
        std::string msg;
        msg += "Input A: ";
        if (!_srcClipA) {
            msg += "N/A";
        } else {
            msg += imageFormatString( _srcClipA->getPixelComponents(), _srcClipA->getPixelDepth() );
        }
        msg += "\n";
        if (getContext() == eContextGeneral) {
            msg += "Input B: ";
            if (!_srcClipB) {
                msg += "N/A";
            } else {
                msg += imageFormatString( _srcClipB->getPixelComponents(), _srcClipB->getPixelDepth() );
            }
            msg += "\n";
        }
        msg += "Output: ";
        if (!_dstClip) {
            msg += "N/A";
        } else {
            msg += imageFormatString( _dstClip->getPixelComponents(), _dstClip->getPixelDepth() );
        }
        msg += "\n";
        sendMessage(Message::eMessageMessage, "", msg);
    }
    updateVisibility();
} // ShufflePlugin::changedParam

void
ShufflePlugin::changedClip(const InstanceChangedArgs & /*args*/,
                           const std::string &clipName)
{
    if ( (getContext() == eContextGeneral) &&
         ( ( clipName == kClipA) || ( clipName == kClipB) ) ) {
        // check that A and B are compatible if they're both connected
        if ( _srcClipA && _srcClipA->isConnected() && _srcClipB && _srcClipB->isConnected() ) {
            BitDepthEnum srcABitDepth = _srcClipA->getPixelDepth();
            BitDepthEnum srcBBitDepth = _srcClipB->getPixelDepth();
            // both input must have the same bit depth
            if (srcABitDepth != srcBBitDepth) {
                setPersistentMessage(Message::eMessageError, "", "Shuffle: both inputs must have the same bit depth");
                throwSuiteStatusException(kOfxStatErrImageFormat);
            }
        }
    }
    updateVisibility();
}

void
ShufflePlugin::enableComponents(void)
{
    {
        switch (gOutputComponentsMap[_outputComponents->getValue()]) {
        case ePixelComponentRGBA:
            for (int i = 0; i < 4; ++i) {
                _channelParam[i]->setEnabled(true);
            }
            break;
        case ePixelComponentRGB:
            _channelParam[0]->setEnabled(true);
            _channelParam[1]->setEnabled(true);
            _channelParam[2]->setEnabled(true);
            _channelParam[3]->setEnabled(false);
            break;
        case ePixelComponentAlpha:
            _channelParam[0]->setEnabled(false);
            _channelParam[1]->setEnabled(false);
            _channelParam[2]->setEnabled(false);
            _channelParam[3]->setEnabled(true);
            break;
#ifdef OFX_EXTENSIONS_NUKE
        case ePixelComponentMotionVectors:
        case ePixelComponentStereoDisparity:
            _channelParam[0]->setEnabled(true);
            _channelParam[1]->setEnabled(true);
            _channelParam[2]->setEnabled(false);
            _channelParam[3]->setEnabled(false);
            break;
#endif
#ifdef OFX_EXTENSIONS_NATRON
        case ePixelComponentXY:
            _channelParam[0]->setEnabled(true);
            _channelParam[1]->setEnabled(true);
            _channelParam[2]->setEnabled(false);
            _channelParam[3]->setEnabled(false);
            break;
#endif
        default:
            assert(0);
            break;
        }
    }
} // ShufflePlugin::enableComponents

mDeclarePluginFactory(ShufflePluginFactory, {ofxsThreadSuiteCheck();}, {});
void
ShufflePluginFactory::describe(ImageEffectDescriptor &desc)
{
    desc.setIsDeprecated(true);
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

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
        // Note: gOutputBitDepthMap must have size # of bit depths + 1 !
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
        assert( sizeof(gOutputBitDepthMap) >= sizeof(gOutputBitDepthMap[0]) * (i + 1) );
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
        // Note: gOutputComponentsMap must have size # of component types + 1 !
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
        assert( sizeof(gOutputComponentsMap) >= sizeof(gOutputComponentsMap[0]) * (i + 1) );
        gOutputComponentsMap[i] = ePixelComponentNone;
    }

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    // say we can support multiple pixel depths on in and out
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
} // ShufflePluginFactory::describe

static void
addInputChannelOtions(ChoiceParamDescriptor* outputR,
                      InputChannelEnum def,
                      ContextEnum context)
{
    assert(outputR->getNOptions() == eInputChannelAR);
    outputR->appendOption(kParamOutputOptionAR);
    assert(outputR->getNOptions() == eInputChannelAG);
    outputR->appendOption(kParamOutputOptionAG);
    assert(outputR->getNOptions() == eInputChannelAB);
    outputR->appendOption(kParamOutputOptionAB);
    assert(outputR->getNOptions() == eInputChannelAA);
    outputR->appendOption(kParamOutputOptionAA);
    assert(outputR->getNOptions() == eInputChannel0);
    outputR->appendOption(kParamOutputOption0);
    assert(outputR->getNOptions() == eInputChannel1);
    outputR->appendOption(kParamOutputOption1);
    if (context == eContextGeneral) {
        assert(outputR->getNOptions() == eInputChannelBR);
        outputR->appendOption(kParamOutputOptionBR);
        assert(outputR->getNOptions() == eInputChannelBG);
        outputR->appendOption(kParamOutputOptionBG);
        assert(outputR->getNOptions() == eInputChannelBB);
        outputR->appendOption(kParamOutputOptionBB);
        assert(outputR->getNOptions() == eInputChannelBA);
        outputR->appendOption(kParamOutputOptionBA);
    }
    outputR->setDefault(def);
}

void
ShufflePluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                        ContextEnum context)
{
    if (context == eContextGeneral) {
        ClipDescriptor* srcClipB = desc.defineClip(kClipB);
        srcClipB->addSupportedComponent(ePixelComponentRGBA);
        srcClipB->addSupportedComponent(ePixelComponentRGB);
        srcClipB->addSupportedComponent(ePixelComponentAlpha);
#ifdef OFX_EXTENSIONS_NATRON
        srcClipB->addSupportedComponent(ePixelComponentXY);
#endif
        srcClipB->setTemporalClipAccess(false);
        srcClipB->setSupportsTiles(kSupportsTiles);
        srcClipB->setOptional(true);

        ClipDescriptor* srcClipA = desc.defineClip(kClipA);
        srcClipA->addSupportedComponent(ePixelComponentRGBA);
        srcClipA->addSupportedComponent(ePixelComponentRGB);
        srcClipA->addSupportedComponent(ePixelComponentAlpha);
#ifdef OFX_EXTENSIONS_NATRON
        srcClipA->addSupportedComponent(ePixelComponentXY);
#endif
        srcClipA->setTemporalClipAccess(false);
        srcClipA->setSupportsTiles(kSupportsTiles);
        srcClipA->setOptional(false);
    } else {
        ClipDescriptor* srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentRGB);
        srcClip->addSupportedComponent(ePixelComponentAlpha);
#ifdef OFX_EXTENSIONS_NATRON
        srcClip->addSupportedComponent(ePixelComponentXY);
#endif
        srcClip->setTemporalClipAccess(false);
        srcClip->setSupportsTiles(kSupportsTiles);
    }
    {
        // create the mandated output clip
        ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
        dstClip->addSupportedComponent(ePixelComponentRGBA);
        dstClip->addSupportedComponent(ePixelComponentRGB);
        dstClip->addSupportedComponent(ePixelComponentAlpha);
#ifdef OFX_EXTENSIONS_NATRON
        dstClip->addSupportedComponent(ePixelComponentXY);
#endif
        dstClip->setSupportsTiles(kSupportsTiles);
    }

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // outputComponents
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamOutputComponents);
        param->setLabel(kParamOutputComponentsLabel);
        param->setHint(kParamOutputComponentsHint);
        // the following must be in the same order as in describe(), so that the map works
        if (gSupportsRGBA) {
            assert(gOutputComponentsMap[param->getNOptions()] == ePixelComponentRGBA);
            param->appendOption(kParamOutputComponentsOptionRGBA);
        }
        if (gSupportsRGB) {
            assert(gOutputComponentsMap[param->getNOptions()] == ePixelComponentRGB);
            param->appendOption(kParamOutputComponentsOptionRGB);
        }
        if (gSupportsAlpha) {
            assert(gOutputComponentsMap[param->getNOptions()] == ePixelComponentAlpha);
            param->appendOption(kParamOutputComponentsOptionAlpha);
        }
        param->setDefault(0);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOutputPremultiplication);
        param->setLabel(kParamOutputPremultiplicationLabel);
        param->setHint(kParamOutputPremultiplicationHint);
        param->setAnimates(false);
        assert(param->getNOptions() == eImageOpaque);
        param->appendOption("Opaque");
        assert(param->getNOptions() == eImagePreMultiplied);
        param->appendOption("Premultiplied");
        assert(param->getNOptions() == eImageUnPreMultiplied);
        param->appendOption("Unpremultiplied");
        param->setDefault( (int)eImageUnPreMultiplied );
        if (page) {
            page->addChild(*param);
        }
        desc.addClipPreferencesSlaveParam(*param);
    }

    // ouputBitDepth
    if (getImageEffectHostDescription()->supportsMultipleClipDepths) {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamOutputBitDepth);
        param->setLabel(kParamOutputBitDepthLabel);
        param->setHint(kParamOutputBitDepthHint);
        // the following must be in the same order as in describe(), so that the map works
        if (gSupportsFloats) {
            // coverity[check_return]
            assert(0 <= param->getNOptions() && param->getNOptions() < 4 && gOutputBitDepthMap[param->getNOptions()] == eBitDepthFloat);
            param->appendOption(kParamOutputBitDepthOptionFloat);
        }
        if (gSupportsShorts) {
            // coverity[check_return]
            assert(0 <= param->getNOptions() && param->getNOptions() < 4 && gOutputBitDepthMap[param->getNOptions()] == eBitDepthUShort);
            param->appendOption(kParamOutputBitDepthOptionShort);
        }
        if (gSupportsBytes) {
            // coverity[check_return]
            assert(0 <= param->getNOptions() && param->getNOptions() < 4 && gOutputBitDepthMap[param->getNOptions()] == eBitDepthUByte);
            param->appendOption(kParamOutputBitDepthOptionByte);
        }
        param->setDefault(0);
#ifndef DEBUG
        // Shuffle only does linear conversion, which is useless for 8-bits and 16-bits formats.
        // Disable it for now (in the future, there may be colorspace conversion options)
        param->setIsSecretAndDisabled(true); // always secret
#endif
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }

    if (gSupportsRGB || gSupportsRGBA) {
        {
            // outputR
            {
                ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamOutputR);
                param->setLabel(kParamOutputRLabel);
                param->setHint(kParamOutputRHint);
                addInputChannelOtions(param, eInputChannelAR, context);
                if (page) {
                    page->addChild(*param);
                }
            }

            // outputG
            {
                ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamOutputG);
                param->setLabel(kParamOutputGLabel);
                param->setHint(kParamOutputGHint);
                addInputChannelOtions(param, eInputChannelAG, context);
                if (page) {
                    page->addChild(*param);
                }
            }

            // outputB
            {
                ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamOutputB);
                param->setLabel(kParamOutputBLabel);
                param->setHint(kParamOutputBHint);
                addInputChannelOtions(param, eInputChannelAB, context);
                if (page) {
                    page->addChild(*param);
                }
            }
        }
    }
    // ouputA
    if (gSupportsRGBA || gSupportsAlpha) {
        {
            {
                ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamOutputA);
                param->setLabel(kParamOutputALabel);
                param->setHint(kParamOutputAHint);
                addInputChannelOtions(param, eInputChannelAA, context);
                if (page) {
                    page->addChild(*param);
                }
            }
        }
    }


    // clipInfo
    {
        PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamClipInfo);
        param->setLabel(kParamClipInfoLabel);
        param->setHint(kParamClipInfoHint);
        if (page) {
            page->addChild(*param);
        }
    }
} // ShufflePluginFactory::describeInContext

ImageEffect*
ShufflePluginFactory::createInstance(OfxImageEffectHandle handle,
                                     ContextEnum context)
{
    return new ShufflePlugin(handle, context);
}

static ShufflePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
