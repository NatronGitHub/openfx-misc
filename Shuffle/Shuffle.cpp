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
#include <set>
#include <algorithm>

#include "ofxsProcessing.H"
#include "ofxsPixelProcessor.h"
#include "ofxsMacros.h"
#include "ofxsCoords.h"
#include "ofxsMultiPlane.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "ShuffleOFX"
#define kPluginGrouping "Channel"
#define kPluginDescription "Rearrange channels from one or two inputs and/or convert to different bit depth or components. No colorspace conversion is done (mapping is linear, even for 8-bit and 16-bit types)."
#define kPluginIdentifier "net.sf.openfx.ShufflePlugin"
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths true // can convert depth
#define kRenderThreadSafety eRenderFullySafe

#define kEnableMultiPlanar true

#define kParamOutputComponents "outputComponents"
#define kParamOutputComponentsLabel "Output Components"
#define kParamOutputComponentsHint "Select what types of components the plug-in should output, this has an effect only when the Output Layer is set to the Color layer." \
    " This controls what should be the components for the Color Layer: Alpha, RGB or RGBA"
#define kParamOutputComponentsOptionRGBA "RGBA", "Output RGBA components.", "rgba"
#define kParamOutputComponentsOptionRGB "RGB", "Output RGB components.", "rgb"
#define kParamOutputComponentsOptionAlpha "Alpha", "Output Alpha component.", "alpha"

#define kParamOutputChannels "outputLayer"
#define kParamOutputChannelsLabel "Output Plane"
#define kParamOutputChannelsHint "The plane that will be written to in output"


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
#define kParamOutputRChoice kParamOutputR "Choice"
#define kParamOutputRRefreshButton kParamOutputR "RefreshButton"
#define kParamOutputRLabel "R"
#define kParamOutputRHint "Input channel for the output red channel"

#define kParamOutputG "outputG"
#define kParamOutputGChoice kParamOutputG "Choice"
#define kParamOutputGRefreshButton kParamOutputG "RefreshButton"
#define kParamOutputGLabel "G"
#define kParamOutputGHint "Input channel for the output green channel"

#define kParamOutputB "outputB"
#define kParamOutputBChoice kParamOutputB "Choice"
#define kParamOutputBRefreshButton kParamOutputB "RefreshButton"
#define kParamOutputBLabel "B"
#define kParamOutputBHint "Input channel for the output blue channel"


#define kParamOutputA "outputA"
#define kParamOutputAChoice kParamOutputA "Choice"
#define kParamOutputARefreshButton kParamOutputA "RefreshButton"
#define kParamOutputALabel "A"
#define kParamOutputAHint "Input channel for the output alpha channel"


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
static bool gIsMultiPlanarV1 = false;
static bool gIsMultiPlanarV2 = false;
static bool gHostIsNatronVersion3OrGreater = false;
static PixelComponentEnum gOutputComponentsMap[4]; // 3 components + a sentinel at the end with ePixelComponentNone
static BitDepthEnum gOutputBitDepthMap[4]; // 3 possible bit depths + a sentinel


class ShufflerBase
    : public ImageProcessor
{
protected:
    const Image *_srcImgA;
    const Image *_srcImgB;
    PixelComponentEnum _outputLayer;
    int _outputComponentCount;
    BitDepthEnum _outputBitDepth;
    std::vector<InputChannelEnum> _channelMap;

public:
    ShufflerBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _srcImgA(NULL)
        , _srcImgB(NULL)
        , _outputLayer(ePixelComponentNone)
        , _outputComponentCount(0)
        , _outputBitDepth(eBitDepthNone)
        , _channelMap()
    {
    }

    void setSrcImg(const Image *A,
                   const Image *B) {_srcImgA = A; _srcImgB = B; }

    void setValues(PixelComponentEnum outputComponents,
                   int outputComponentCount,
                   BitDepthEnum outputBitDepth,
                   const std::vector<InputChannelEnum> &channelMap)
    {
        _outputLayer = outputComponents,
        _outputComponentCount = outputComponentCount,
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

struct InputPlaneChannel
{
    Image* img;
    int channelIndex;
    bool fillZero;

    InputPlaneChannel() : img(NULL), channelIndex(-1), fillZero(true) {}
};

class MultiPlaneShufflerBase
    : public ImageProcessor
{
protected:

    int _outputComponentCount;
    BitDepthEnum _outputBitDepth;
    int _nComponentsDst;
    std::vector<InputPlaneChannel> _inputPlanes;

public:
    MultiPlaneShufflerBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _outputComponentCount(0)
        , _outputBitDepth(eBitDepthNone)
        , _nComponentsDst(0)
        , _inputPlanes(_nComponentsDst)
    {
    }

    void setValues(int outputComponentCount,
                   BitDepthEnum outputBitDepth,
                   const std::vector<InputPlaneChannel>& planes)
    {
        _outputComponentCount = outputComponentCount,
        _outputBitDepth = outputBitDepth;
        _inputPlanes = planes;
    }
};


template <class PIXSRC, class PIXDST, int nComponentsDst>
class MultiPlaneShuffler
    : public MultiPlaneShufflerBase
{
public:
    MultiPlaneShuffler(ImageEffect &instance)
        : MultiPlaneShufflerBase(instance)
    {
    }

private:
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        assert(_inputPlanes.size() == nComponentsDst);
        // now compute the transformed image, component by component

        int srcChannels[nComponentsDst];
        for (int c = 0; c < nComponentsDst; ++c) {
            const Image* srcImg = _inputPlanes[c].img;
            srcChannels[c] = _inputPlanes[c].channelIndex;
            if (!srcImg) {
                srcChannels[c] = _inputPlanes[c].fillZero ? 0 : 1;
            }
            assert( !srcImg || ( srcChannels[c] >= 0 && srcChannels[c] < srcImg->getPixelComponentCount() ) );

        }

        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIXDST *dstPix = (PIXDST *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {

                for (int c = 0; c < nComponentsDst; ++c) {
                    // if there is a srcImg but we are outside of its RoD, it should be considered black and transparent
                    PIXSRC *srcPix = (PIXSRC *)  (_inputPlanes[c].img ? _inputPlanes[c].img->getPixelAddress(x, y) : 0);

                    if (!_inputPlanes[c].img) {
                        // No input image: this is a constant value.
                        // Use constant value depending on fillZero
                        dstPix[c] = convertPixelDepth<float, PIXDST>(srcChannels[c]);
                    } else {
                        // Be black and transparent if we are outside of _inputPlanes[c].img's RoD
                        dstPix[c] = convertPixelDepth<PIXSRC, PIXDST>(srcPix ? srcPix[srcChannels[c]] : 0);
                    }

                }
                dstPix += nComponentsDst;

            }
        }
    }
    
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ShufflePlugin
    : public MultiPlane::MultiPlaneEffect
{
public:
    /** @brief ctor */
    ShufflePlugin(OfxImageEffectHandle handle,
                  ContextEnum context)
        : MultiPlane::MultiPlaneEffect(handle)
        , _dstClip(NULL)
        , _srcClipA(NULL)
        , _srcClipB(NULL)
        , _outputLayer(NULL)
        , _outputBitDepth(NULL)
        , _channelParam()
        , _outputComponents(NULL)
    {
        _channelStringParam[0] = _channelStringParam[1] = _channelStringParam[2] = _channelStringParam[3] = 0;
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (1 <= _dstClip->getPixelComponentCount() && _dstClip->getPixelComponentCount() <= 4) );
        _srcClipA = fetchClip(context == eContextGeneral ? kClipA : kOfxImageEffectSimpleSourceClipName);
        assert( _srcClipA && (1 <= _srcClipA->getPixelComponentCount() && _srcClipA->getPixelComponentCount() <= 4) );
        if (context == eContextGeneral) {
            _srcClipB = fetchClip(kClipB);
            assert( _srcClipB && (1 <= _srcClipB->getPixelComponentCount() && _srcClipB->getPixelComponentCount() <= 4) );
        }
        if (gIsMultiPlanarV1 || gIsMultiPlanarV2) {
            _outputLayer = fetchChoiceParam(kParamOutputChannels);
        }
        if (getImageEffectHostDescription()->supportsMultipleClipDepths) {
            _outputBitDepth = fetchChoiceParam(kParamOutputBitDepth);
        }
        _channelParam[0] = fetchChoiceParam(kParamOutputR);
        _channelParam[1] = fetchChoiceParam(kParamOutputG);
        _channelParam[2] = fetchChoiceParam(kParamOutputB);
        _channelParam[3] = fetchChoiceParam(kParamOutputA);
        try {
            if (paramExists(kParamOutputRChoice)) {
                _channelStringParam[0] = fetchStringParam(kParamOutputRChoice);
                _channelStringParam[1] = fetchStringParam(kParamOutputGChoice);
                _channelStringParam[2] = fetchStringParam(kParamOutputBChoice);
                _channelStringParam[3] = fetchStringParam(kParamOutputAChoice);
            }
        } catch (...) {

        }

        _outputComponents = fetchChoiceParam(kParamOutputComponents);

        if (gIsMultiPlanarV1 || gIsMultiPlanarV2) {
            std::vector<Clip*> abClips(2);
            abClips[0] = _srcClipA;
            abClips[1] = _srcClipB;
            {
                FetchChoiceParamOptions args = FetchChoiceParamOptions::createFetchChoiceParamOptionsForInputChannel();
                args.dependsClips = abClips;
                fetchDynamicMultiplaneChoiceParameter(kParamOutputR, args);
            }
            {
                FetchChoiceParamOptions args = FetchChoiceParamOptions::createFetchChoiceParamOptionsForInputChannel();
                args.dependsClips = abClips;
                fetchDynamicMultiplaneChoiceParameter(kParamOutputG, args);
            }
            {
                FetchChoiceParamOptions args = FetchChoiceParamOptions::createFetchChoiceParamOptionsForInputChannel();
                args.dependsClips = abClips;
                fetchDynamicMultiplaneChoiceParameter(kParamOutputB, args);
            }
            {
                FetchChoiceParamOptions args = FetchChoiceParamOptions::createFetchChoiceParamOptionsForInputChannel();
                args.dependsClips = abClips;
                fetchDynamicMultiplaneChoiceParameter(kParamOutputA, args);
            }
            {
                FetchChoiceParamOptions args = FetchChoiceParamOptions::createFetchChoiceParamOptionsForOutputPlane();
                args.dependsClips.push_back(_srcClipA);
                fetchDynamicMultiplaneChoiceParameter(kParamOutputChannels, args);
            }
            onAllParametersFetched();
        }

        _outputPremult = fetchChoiceParam(kParamOutputPremultiplication);

    }

private:

    void setChannelsFromRed(double time);

    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /* override is identity */
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;
    virtual OfxStatus getClipComponents(const ClipComponentsArguments& args, ClipComponentsSetter& clipComponents) OVERRIDE FINAL;

    /** @brief get the clip preferences */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /* override changedParam */
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

private:

    void setOutputComponentsParam(PixelComponentEnum comps);

    bool isIdentityInternal(double time, Clip*& identityClip);

    void updateInputChannelsVisibility(const MultiPlane::ImagePlaneDesc& plane);


    /* internal render function */
    template <class DSTPIX, int nComponentsDst>
    void renderInternalForDstBitDepth(const RenderArguments &args, BitDepthEnum srcBitDepth, const MultiPlane::ImagePlaneDesc &dstPlane, Image* dstImage);

    template <int nComponentsDst>
    void renderInternal(const RenderArguments &args, BitDepthEnum srcBitDepth, BitDepthEnum dstBitDepth, const MultiPlane::ImagePlaneDesc &dstPlane, Image* dstImage);

    /* set up and run a processor */
    void setupAndProcess(ShufflerBase &, const RenderArguments &args, Image* dstImage);
    void setupAndProcessMultiPlane(MultiPlaneShufflerBase &, const RenderArguments &args, Image* dstImage);

    MultiPlane::ImagePlaneDesc getPlaneFromOutputComponents() const;

    MultiPlane::ImagePlaneDesc getDstPixelComps();

    void updateOutputParamsVisibility(const MultiPlane::ImagePlaneDesc& plane);

    // This is called by Natron 3 in clipChanged on the outputClip: it replaces getClipPreferences since the results
    // of getClipPreferences are cached and thus it is not always called.
    // @param dstPlaneOut [out] If non null, will be set to the plane written to in output
    void onMetadataChanged(MultiPlane::ImagePlaneDesc* dstPlaneOut);

    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClipA;
    Clip *_srcClipB;

    // The output plane selected by the user
    ChoiceParam *_outputLayer;
    ChoiceParam *_outputBitDepth;

    // The input plane for each channel
    ChoiceParam* _channelParam[4];
    StringParam* _channelStringParam[4];

    // Components type when the output layer is the color plane (Alpha - RGB - RGBA)
    ChoiceParam *_outputComponents;
    ChoiceParam *_outputPremult;
};

OfxStatus
ShufflePlugin::getClipComponents(const ClipComponentsArguments& args,
                                 ClipComponentsSetter& clipComponents)
{
    assert(gIsMultiPlanarV2 || gIsMultiPlanarV1);
    OfxStatus stat = MultiPlaneEffect::getClipComponents(args, clipComponents);
    clipComponents.setPassThroughClip(_srcClipA, args.time, args.view);
    return stat;
}

struct IdentityChoiceData
{
    Clip* clip;
    MultiPlane::ImagePlaneDesc plane;
    int index;

    IdentityChoiceData()
    : clip(NULL)
    , plane()
    , index(-1)
    {

    }
};

bool
ShufflePlugin::isIdentityInternal(double time,
                                  Clip*& identityClip)
{
    PixelComponentEnum srcAComponents = _srcClipA ? _srcClipA->getPixelComponents() : ePixelComponentNone;
    PixelComponentEnum srcBComponents = _srcClipB ? _srcClipB->getPixelComponents() : ePixelComponentNone;
    PixelComponentEnum dstComponents = _dstClip ? _dstClip->getPixelComponents() : ePixelComponentNone;

    if (!gIsMultiPlanarV2 && !gIsMultiPlanarV1) {
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

        return false;
    } else {
        IdentityChoiceData data[4];

        MultiPlane::ImagePlaneDesc dstPlane;
        {
            OFX::Clip* clip = 0;
            int channelIndex = -1;
            MultiPlane::MultiPlaneEffect::GetPlaneNeededRetCodeEnum stat = getPlaneNeeded(_outputLayer->getName(), &clip, &dstPlane, &channelIndex);
            if (stat != MultiPlane::MultiPlaneEffect::eGetPlaneNeededRetCodeReturnedPlane) {
                return false;
            }
        }
        

        int expectedIndex = -1;
        for (int i = 0; i < 4; ++i) {
            MultiPlane::MultiPlaneEffect::GetPlaneNeededRetCodeEnum stat = getPlaneNeeded(_channelParam[i]->getName(), &data[i].clip, &data[i].plane, &data[i].index);

            if (stat != MultiPlane::MultiPlaneEffect::eGetPlaneNeededRetCodeReturnedChannelInPlane) {
                return false;
            }
            if (!data[i].plane.isColorPlane()) {
                if (i != 3) {
                    //This is not the color plane, no identity
                    return false;
                } else {

                    //In this case if the A choice is visible, the user either checked "Create alpha" or he/she set it explicitly to 0 or 1
                    if (data[i].index == 3 && !_channelParam[3]->getIsSecret()) {
                        return false;
                    } else {
                        ///Do not do the checks below
                        continue;
                    }
                }
            }
            if (i > 0) {
                if ( (data[i].index != expectedIndex) || (data[i].plane != data[0].plane) ||
                     ( data[i].clip != data[0].clip) ) {
                    return false;
                }
            }
            expectedIndex = data[i].index + 1;
        }

        // 2 planes might not be equal but be the color plane (e.g RGB and RGBA)
        if (data[0].plane != dstPlane && data[0].plane.isColorPlane() != dstPlane.isColorPlane()) {
            return false;
        }
        identityClip = data[0].clip;

        return true;
    }
} // ShufflePlugin::isIdentityInternal

bool
ShufflePlugin::isIdentity(const IsIdentityArguments &args,
                          Clip * &identityClip,
                          double & /*identityTime*/
                          , int& /*view*/, std::string& /*plane*/)
{
    const double time = args.time;

    return isIdentityInternal(time, identityClip);
}

bool
ShufflePlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                     OfxRectD &rod)
{
    const double time = args.time;
    Clip* identityClip = 0;

    if ( isIdentityInternal(time, identityClip) ) {
        rod = identityClip->getRegionOfDefinition(args.time);

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
                               const RenderArguments &args,
                               Image* dst)
{

    const double time = args.time;
    BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    PixelComponentEnum dstComponents  = dst->getPixelComponents();
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
    int outputComponentCount = dst->getPixelComponentCount();

    processor.setValues(outputComponents, outputComponentCount, outputBitDepth, channelMap);

    processor.setDstImg(dst);
    processor.setRenderWindow(args.renderWindow);

    processor.process();
} // ShufflePlugin::setupAndProcess

class InputImagesHolder_RAII
{
    std::vector<Image*> images;

public:

    InputImagesHolder_RAII()
        : images()
    {
    }

    void appendImage(Image* img)
    {
        images.push_back(img);
    }

    ~InputImagesHolder_RAII()
    {
        for (std::size_t i = 0; i < images.size(); ++i) {
            delete images[i];
        }
    }
};

void
ShufflePlugin::setupAndProcessMultiPlane(MultiPlaneShufflerBase & processor,
                                         const RenderArguments &args,
                                         Image* dst)
{
    const double time = args.time;
    
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);
    }


    InputImagesHolder_RAII imagesHolder;
    BitDepthEnum srcBitDepth = eBitDepthNone;
    std::map<Clip*, std::map<std::string, Image*> > fetchedPlanes;
    std::vector<InputPlaneChannel> planes;
    int dstNComps = dst->getPixelComponentCount();
    for (int i = 0; i < dstNComps; ++i) {
        InputPlaneChannel p;
        Clip* clip = 0;
        MultiPlane::ImagePlaneDesc plane;
        MultiPlane::MultiPlaneEffect::GetPlaneNeededRetCodeEnum stat = getPlaneNeeded(dstNComps == 1 ? _channelParam[3]->getName() : _channelParam[i]->getName(), &clip, &plane, &p.channelIndex);
        if (stat == MultiPlane::MultiPlaneEffect::eGetPlaneNeededRetCodeFailed) {
            setPersistentMessage(Message::eMessageError, "", "Cannot find requested channels in input");
            throwSuiteStatusException(kOfxStatFailed);
            return;
        }


        p.img = 0;
        if (stat == MultiPlane::MultiPlaneEffect::eGetPlaneNeededRetCodeReturnedConstant0 || (stat == MultiPlane::MultiPlaneEffect::eGetPlaneNeededRetCodeReturnedChannelInPlane && plane.getNumComponents() == 0)) {
            p.fillZero = true;
        } else if (stat == MultiPlane::MultiPlaneEffect::eGetPlaneNeededRetCodeReturnedConstant1) {
            p.fillZero = false;
        } else {
            std::map<std::string, Image*>& clipPlanes = fetchedPlanes[clip];
            std::string ofxPlaneString = MultiPlane::ImagePlaneDesc::mapPlaneToOFXPlaneString(plane);
            std::map<std::string, Image*>::iterator foundPlane = clipPlanes.find(ofxPlaneString);
            if ( foundPlane != clipPlanes.end() ) {
                p.img = foundPlane->second;
            } else {
                p.img = clip->fetchImagePlane( args.time, args.renderView, ofxPlaneString.c_str() );
                if (p.img) {
                    clipPlanes.insert( std::make_pair(plane.getPlaneID(), p.img) );
                    imagesHolder.appendImage(p.img);
                }
            }
        }

        if (p.img) {
            if ( (p.img->getRenderScale().x != args.renderScale.x) ||
                 ( p.img->getRenderScale().y != args.renderScale.y) ||
                 ( ( p.img->getField() != eFieldNone) /* for DaVinci Resolve */ && ( p.img->getField() != args.fieldToRender) ) ) {
                setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                throwSuiteStatusException(kOfxStatFailed);
            }
            if (srcBitDepth == eBitDepthNone) {
                srcBitDepth = p.img->getPixelDepth();
            } else {
                // both input must have the same bit depth and components
                if ( (srcBitDepth != eBitDepthNone) && ( srcBitDepth != p.img->getPixelDepth() ) ) {
                    throwSuiteStatusException(kOfxStatErrImageFormat);
                }
            }
            if ( (p.channelIndex < 0) || ( p.channelIndex >= (int)p.img->getPixelComponentCount() ) ) {
                throwSuiteStatusException(kOfxStatErrImageFormat);
            }
        }
        planes.push_back(p);
    }

    BitDepthEnum outputBitDepth = srcBitDepth;
    if (getImageEffectHostDescription()->supportsMultipleClipDepths) {
        outputBitDepth = gOutputBitDepthMap[_outputBitDepth->getValueAtTime(time)];
    }

    processor.setValues(dstNComps, outputBitDepth, planes);

    processor.setDstImg(dst);
    processor.setRenderWindow(args.renderWindow);

    processor.process();
} // ShufflePlugin::setupAndProcessMultiPlane

template <class DSTPIX, int nComponentsDst>
void
ShufflePlugin::renderInternalForDstBitDepth(const RenderArguments &args,
                                            BitDepthEnum srcBitDepth,
                                            const MultiPlane::ImagePlaneDesc &/*dstPlane*/,
                                            Image* dstImage)
{
    if (!gIsMultiPlanarV2 && !gIsMultiPlanarV1) {
        switch (srcBitDepth) {
        case eBitDepthUByte: {
            Shuffler<unsigned char, DSTPIX, nComponentsDst> fred(*this);
            setupAndProcess(fred, args, dstImage);
            break;
        }
        case eBitDepthUShort: {
            Shuffler<unsigned short, DSTPIX, nComponentsDst> fred(*this);
            setupAndProcess(fred, args, dstImage);
            break;
        }
        case eBitDepthFloat: {
            Shuffler<float, DSTPIX, nComponentsDst> fred(*this);
            setupAndProcess(fred, args, dstImage);
            break;
        }
        default:
            throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else {
        switch (srcBitDepth) {
        case eBitDepthUByte: {
            MultiPlaneShuffler<unsigned char, DSTPIX, nComponentsDst> fred(*this);
            setupAndProcessMultiPlane(fred, args, dstImage);
            break;
        }
        case eBitDepthUShort: {
            MultiPlaneShuffler<unsigned short, DSTPIX, nComponentsDst> fred(*this);
            setupAndProcessMultiPlane(fred, args, dstImage);
            break;
        }
        case eBitDepthFloat: {
            MultiPlaneShuffler<float, DSTPIX, nComponentsDst> fred(*this);
            setupAndProcessMultiPlane(fred, args, dstImage);
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
                              BitDepthEnum dstBitDepth,
                              const MultiPlane::ImagePlaneDesc &dstPlane, Image* dstImage)
{
    switch (dstBitDepth) {
    case eBitDepthUByte:
        renderInternalForDstBitDepth<unsigned char, nComponentsDst>(args, srcBitDepth, dstPlane, dstImage);
        break;
    case eBitDepthUShort:
        renderInternalForDstBitDepth<unsigned short, nComponentsDst>(args, srcBitDepth, dstPlane, dstImage);
        break;
    case eBitDepthFloat:
        renderInternalForDstBitDepth<float, nComponentsDst>(args, srcBitDepth, dstPlane, dstImage);
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
#ifdef DEBUG
    // Follow the OpenFX spec:
    // check that dstComponents is consistent with the result of getClipPreferences
    // (@see getClipPreferences).
    if (!gIsMultiPlanarV2 && !gIsMultiPlanarV1) {
        // set the components of _dstClip
        PixelComponentEnum outputComponents = gOutputComponentsMap[_outputComponents->getValueAtTime(time)];
        assert(dstComponents == outputComponents);
    }

    if (getImageEffectHostDescription()->supportsMultipleClipDepths) {
        // set the bitDepth of _dstClip
        BitDepthEnum outputBitDepth = gOutputBitDepthMap[_outputBitDepth->getValueAtTime(time)];
        assert(dstBitDepth == outputBitDepth);
    }
#endif


    MultiPlane::ImagePlaneDesc dstPlane;
    if (!gIsMultiPlanarV1 && !gIsMultiPlanarV2) {
        int dstComponentCount = _dstClip->getPixelComponentCount();
        dstPlane = MultiPlane::ImagePlaneDesc::mapNCompsToColorPlane(dstComponentCount);
    } else {

        OFX::Clip* clip = 0;
        int channelIndex = -1;
        MultiPlane::MultiPlaneEffect::GetPlaneNeededRetCodeEnum stat = getPlaneNeeded(_outputLayer->getName(), &clip, &dstPlane, &channelIndex);
        if (stat != MultiPlane::MultiPlaneEffect::eGetPlaneNeededRetCodeReturnedPlane) {
            throwSuiteStatusException(kOfxStatFailed);
            return;
        }

    }
    assert(1 <= dstPlane.getNumComponents() && dstPlane.getNumComponents() <= 4);



    assert( kSupportsMultipleClipPARs   || _srcClipA->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || _srcClipA->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert( kSupportsMultipleClipPARs   || _srcClipB->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || _srcClipB->getPixelDepth()       == _dstClip->getPixelDepth() );
    // get the components of _dstClip

    if (!gIsMultiPlanarV2) {
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

    auto_ptr<Image> dst;
    if (!gIsMultiPlanarV2 && !gIsMultiPlanarV1) {
        dst.reset( _dstClip->fetchImage(args.time) );

        if ( !dst.get() ) {
            throwSuiteStatusException(kOfxStatFailed);
        }
        BitDepthEnum dstBitDepth    = dst->getPixelDepth();
        PixelComponentEnum dstComponents  = dst->getPixelComponents();
        if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
            ( dstComponents != _dstClip->getPixelComponents() ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
            throwSuiteStatusException(kOfxStatFailed);
        }

    } else {
        dst.reset( _dstClip->fetchImagePlane( args.time, args.renderView, MultiPlane::ImagePlaneDesc::mapPlaneToOFXPlaneString(dstPlane).c_str() ) );
        if ( !dst.get() ) {
            throwSuiteStatusException(kOfxStatFailed);
        }

        // In multiplane mode, we cannot expect the image plane to match the clip components
        if (dstBitDepth != _dstClip->getPixelDepth()) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
            throwSuiteStatusException(kOfxStatFailed);
        }
    }

    int dstComponentCount = dst->getPixelComponentCount();
    switch (dstComponentCount) {
    case 4:
        renderInternal<4>(args, srcBitDepth, dstBitDepth, dstPlane, dst.get());
        break;
    case 3:
        renderInternal<3>(args, srcBitDepth, dstBitDepth, dstPlane, dst.get());
        break;
    case 2:
        renderInternal<2>(args, srcBitDepth, dstBitDepth, dstPlane, dst.get());
        break;
    case 1:
        renderInternal<1>(args, srcBitDepth, dstBitDepth, dstPlane, dst.get());
        break;
    }
} // ShufflePlugin::render

MultiPlane::ImagePlaneDesc
ShufflePlugin::getPlaneFromOutputComponents() const
{
    PixelComponentEnum comps = gOutputComponentsMap[_outputComponents->getValue()];
    switch (comps) {
        case OFX::ePixelComponentRGB:
            return MultiPlane::ImagePlaneDesc::getRGBComponents();
        case OFX::ePixelComponentAlpha:
            return MultiPlane::ImagePlaneDesc::getAlphaComponents();
        case OFX::ePixelComponentRGBA:
            return MultiPlane::ImagePlaneDesc::getRGBAComponents();
        default:
            return MultiPlane::ImagePlaneDesc::getNoneComponents();
    }

}

MultiPlane::ImagePlaneDesc
ShufflePlugin::getDstPixelComps()
{


    if (!gIsMultiPlanarV2 && !gIsMultiPlanarV1) {
        return getPlaneFromOutputComponents();
    } else {
        MultiPlane::ImagePlaneDesc dstPlane;
        {
            OFX::Clip* clip = 0;
            int channelIndex = -1;
            MultiPlane::MultiPlaneEffect::GetPlaneNeededRetCodeEnum stat = getPlaneNeeded(_outputLayer->getName(), &clip, &dstPlane, &channelIndex);
            if (stat != MultiPlane::MultiPlaneEffect::eGetPlaneNeededRetCodeReturnedPlane) {
                return MultiPlane::ImagePlaneDesc::getNoneComponents();
            }
        }
        if (dstPlane.isColorPlane()) {
            // If color plane, select the value chosen by the user from the output components choice
            return getPlaneFromOutputComponents();
        } else {
            return dstPlane;
        }
    }
} // getDstPixelComps

void
ShufflePlugin::onMetadataChanged(MultiPlane::ImagePlaneDesc* dstPlaneOut)
{
    MultiPlane::ImagePlaneDesc dstPlane = getDstPixelComps();
    // Enable components
    updateInputChannelsVisibility(dstPlane);
    updateOutputParamsVisibility(dstPlane);
    if (dstPlaneOut) {
        *dstPlaneOut = dstPlane;
    }
}

void
ShufflePlugin::updateOutputParamsVisibility(const MultiPlane::ImagePlaneDesc& plane)
{


    if (gIsMultiPlanarV2 || gIsMultiPlanarV1) {
        bool secret;
        if (plane.isColorPlane()) {
            // If color plane, select the value chosen by the user from the output components choice
            secret = false;
        } else {
            secret = true;
        }
        _outputComponents->setIsSecretAndDisabled(secret);
    }
    // Premult is only needed for RGBA
    _outputPremult->setIsSecretAndDisabled(plane != MultiPlane::ImagePlaneDesc::getRGBAComponents());
}

/* Override the clip preferences */
void
ShufflePlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{

    // Refresh channel selectors in the base class
    MultiPlaneEffect::getClipPreferences(clipPreferences);

    // Update visibility of parameters depending on the output plane and components type
    MultiPlane::ImagePlaneDesc dstPlane;
    onMetadataChanged(&dstPlane);

    PixelComponentEnum dstPixelComps;
    PixelComponentEnum srcAComps = _srcClipA->getUnmappedPixelComponents();
    PixelComponentEnum srcBComps = _srcClipB->getUnmappedPixelComponents();
    if (dstPlane.isColorPlane()) {
        // If the output plane is the color plane, set the pixel components to one of the OpenFX defaults selected by the user
        dstPixelComps = gOutputComponentsMap[_outputComponents->getValue()];
    } else {
        // A custom plane is selected, set the color plane number of components to the number of components of the custom plane
        //MultiPlane::ImagePlaneDesc colorPlaneMapped = MultiPlane::ImagePlaneDesc::mapNCompsToColorPlane(dstPlane.getNumComponents());
        //dstPixelComps = mapStrToPixelComponentEnum(MultiPlane::ImagePlaneDesc::mapPlaneToOFXComponentsTypeString(colorPlaneMapped));
        dstPixelComps = srcAComps;
    }
    clipPreferences.setClipComponents(*_dstClip, dstPixelComps);
    clipPreferences.setClipComponents(*_srcClipA, srcAComps);
    clipPreferences.setClipComponents(*_srcClipB, srcBComps);

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

static bool
endsWith(const std::string &str,
         const std::string &suffix)
{
    return ( ( str.size() >= suffix.size() ) &&
             (str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0) );
}

void
ShufflePlugin::setChannelsFromRed(double time)
{
    int r_i;

    _channelParam[0]->getValueAtTime(time, r_i);
    std::string rChannel;
    _channelParam[0]->getOption(r_i, rChannel);

    if ( endsWith(rChannel, ".R") || endsWith(rChannel, ".r") ) {
        std::string base = rChannel.substr(0, rChannel.size() - 2);
        bool gSet = false;
        bool bSet = false;
        bool aSet = false;
        int nOpt = _channelParam[1]->getNOptions();
        int indexOf0 = -1;
        int indexOf1 = -1;

        for (int i = 0; i < nOpt; ++i) {
            std::string opt;
            _channelParam[0]->getOption(i, opt);

            if (opt == kMultiPlaneChannelParamOption0) {
                indexOf0 = i;
            } else if (opt == kMultiPlaneChannelParamOption1) {
                indexOf1 = i;
            } else if (opt.substr( 0, base.size() ) == base) {
                std::string chan = opt.substr( base.size() );
                if ( (chan == ".G") || (chan == ".g") ) {
                    _channelParam[1]->setValue(i);
                    if (_channelStringParam[1]) {
                        _channelStringParam[1]->setValue(opt);
                    }
                    gSet = true;
                } else if ( (chan == ".B") || (chan == ".b") ) {
                    _channelParam[2]->setValue(i);
                    if (_channelStringParam[2]) {
                        _channelStringParam[2]->setValue(opt);
                    }
                    bSet = true;
                } else if ( (chan == ".A") || (chan == ".a") ) {
                    _channelParam[3]->setValue(i);
                    if (_channelStringParam[3]) {
                        _channelStringParam[3]->setValue(opt);
                    }
                    aSet = true;
                }
            }
            if ( gSet && bSet && aSet && (indexOf0 != -1) && (indexOf1 != -1) ) {
                // we're done
                break;
            }
        }
        assert(indexOf0 != -1 && indexOf1 != -1);
        if (!gSet) {
            _channelParam[1]->setValue(indexOf0);
        }
        if (!bSet) {
            _channelParam[2]->setValue(indexOf0);
        }
        if (!aSet) {
            _channelParam[3]->setValue(indexOf1);
        }
    }
} // ShufflePlugin::setChannelsFromRed

void
ShufflePlugin::setOutputComponentsParam(PixelComponentEnum components)
{
    assert(components == ePixelComponentRGB || components == ePixelComponentRGBA || components == ePixelComponentAlpha);
    int index = -1;
    for (int i = 0; i < 4; ++i) {
        if (gOutputComponentsMap[i] == ePixelComponentNone) {
            break;
        }
        if (gOutputComponentsMap[i] == components) {
            index = i;
            break;
        }
    }
    assert(index != -1);
    _outputComponents->setValue(index);
}

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
    } else if (paramName == _channelParam[0]->getName() && (args.reason == eChangeUserEdit)) {
#ifdef OFX_EXTENSIONS_NATRON
        setChannelsFromRed(args.time);
#endif
    } else {
        MultiPlaneEffect::changedParam(args, paramName);
    }

} // ShufflePlugin::changedParam

void
ShufflePlugin::changedClip(const InstanceChangedArgs & args,
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

    MultiPlaneEffect::changedClip(args, clipName);
    onMetadataChanged(NULL /*dstPlaneOut*/);

}

void
ShufflePlugin::updateInputChannelsVisibility(const MultiPlane::ImagePlaneDesc& plane)
{
    const std::vector<std::string>& compNames = plane.getChannels();
    if (compNames.size() == 1) {
        _channelParam[0]->setIsSecretAndDisabled(true);
        _channelParam[1]->setIsSecretAndDisabled(true);
        _channelParam[2]->setIsSecretAndDisabled(true);
        _channelParam[3]->setIsSecretAndDisabled(false);
        _channelParam[3]->setLabel(compNames[0]);
    } else if (compNames.size() == 2) {
        _channelParam[0]->setIsSecretAndDisabled(false);
        _channelParam[0]->setLabel(compNames[0]);
        _channelParam[1]->setIsSecretAndDisabled(false);
        _channelParam[1]->setLabel(compNames[1]);
        _channelParam[2]->setIsSecretAndDisabled(true);
        _channelParam[3]->setIsSecretAndDisabled(true);
    } else if (compNames.size() == 3) {
        _channelParam[0]->setIsSecretAndDisabled(false);
        _channelParam[0]->setLabel(compNames[0]);
        _channelParam[1]->setLabel(compNames[1]);
        _channelParam[1]->setIsSecretAndDisabled(false);
        _channelParam[2]->setIsSecretAndDisabled(false);
        _channelParam[2]->setLabel(compNames[2]);
        _channelParam[3]->setIsSecretAndDisabled(true);
    } else if (compNames.size() == 4) {
        _channelParam[0]->setLabel(compNames[0]);
        _channelParam[0]->setIsSecretAndDisabled(false);
        _channelParam[1]->setLabel(compNames[1]);
        _channelParam[1]->setIsSecretAndDisabled(false);
        _channelParam[2]->setIsSecretAndDisabled(false);
        _channelParam[2]->setLabel(compNames[2]);
        _channelParam[3]->setIsSecretAndDisabled(false);
        _channelParam[3]->setLabel(compNames[3]);
    }

} // ShufflePlugin::updateInputChannelsVisibility

mDeclarePluginFactory(ShufflePluginFactory, {ofxsThreadSuiteCheck();}, {});
void
ShufflePluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);


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

    bool supportsDynamicChoices = false;
#ifdef OFX_EXTENSIONS_NATRON
    if (getImageEffectHostDescription()->isNatron && getImageEffectHostDescription()->versionMajor >= 3) {
        gHostIsNatronVersion3OrGreater = true;
    }
    supportsDynamicChoices = getImageEffectHostDescription()->supportsDynamicChoices;

    //Do not add channel selectors, it is pointless
    desc.setChannelSelector(ePixelComponentNone);
#endif
#ifdef OFX_EXTENSIONS_NUKE
    gIsMultiPlanarV1 = kEnableMultiPlanar && getImageEffectHostDescription()->isMultiPlanar;
    gIsMultiPlanarV2 = gIsMultiPlanarV1 && supportsDynamicChoices && fetchSuite(kFnOfxImageEffectPlaneSuite, 2);
    if (gIsMultiPlanarV1 || gIsMultiPlanarV2) {
        // This enables fetching different planes from the input.
        // Generally the user will read a multi-layered EXR file in the Reader node and then use the shuffle
        // to redirect the plane's channels into RGBA color plane.

        desc.setIsMultiPlanar(true);

        // We are pass-through in output, meaning another shuffle below could very well
        // access all planes again. Note that for multi-planar effects this is mandatory to be called
        // since default is false.
        desc.setPassThroughForNotProcessedPlanes(ePassThroughLevelPassThroughNonRenderedPlanes);
    }
#else
    gIsMultiPlanar = false;
#endif
} // ShufflePluginFactory::describe

void
ShufflePluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                        ContextEnum context)
{
#ifdef OFX_EXTENSIONS_NUKE
    if ( gIsMultiPlanarV2 && !fetchSuite(kFnOfxImageEffectPlaneSuite, 2) ) {
        throwHostMissingSuiteException(kFnOfxImageEffectPlaneSuite);
    } else if ( gIsMultiPlanarV1 && !fetchSuite(kFnOfxImageEffectPlaneSuite, 1) ) {
        throwHostMissingSuiteException(kFnOfxImageEffectPlaneSuite);
    }
#endif

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
    if (gIsMultiPlanarV1 || gIsMultiPlanarV2) {
        // defines kParamOutputChannels
        MultiPlane::Factory::describeInContextAddPlaneChoice(desc, page, kParamOutputChannels, kParamOutputChannelsLabel, kParamOutputChannelsHint);
    }
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

    std::vector<std::string> clipsForChannels(2);
    clipsForChannels[0] = kClipA;
    clipsForChannels[1] = kClipB;

    if (gSupportsRGB || gSupportsRGBA) {
        // outputR
        if (gIsMultiPlanarV1 || gIsMultiPlanarV2) {
            ChoiceParamDescriptor* r = MultiPlane::Factory::describeInContextAddPlaneChannelChoice(desc, page, clipsForChannels, kParamOutputR, kParamOutputRLabel, kParamOutputRHint);
            r->setDefault(eInputChannelAR);
            ChoiceParamDescriptor* g = MultiPlane::Factory::describeInContextAddPlaneChannelChoice(desc, page, clipsForChannels, kParamOutputG, kParamOutputGLabel, kParamOutputGHint);
            g->setDefault(eInputChannelAG);
            ChoiceParamDescriptor* b = MultiPlane::Factory::describeInContextAddPlaneChannelChoice(desc, page, clipsForChannels, kParamOutputB, kParamOutputBLabel, kParamOutputBHint);
            b->setDefault(eInputChannelAB);
        } else {
            {
                ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamOutputR);
                param->setLabel(kParamOutputRLabel);
                param->setHint(kParamOutputRHint);
                MultiPlane::Factory::addInputChannelOptionsRGBA(param, clipsForChannels, true /*addConstants*/, true /*onlyColorPlane*/);
                param->setDefault(eInputChannelAR);
                if (page) {
                    page->addChild(*param);
                }
            }
            {
                ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamOutputG);
                param->setLabel(kParamOutputGLabel);
                param->setHint(kParamOutputGHint);
                MultiPlane::Factory::addInputChannelOptionsRGBA(param, clipsForChannels, true /*addConstants*/, true /*onlyColorPlane*/);
                param->setDefault(eInputChannelAG);
                if (page) {
                    page->addChild(*param);
                }
            }
            {
                ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamOutputB);
                param->setLabel(kParamOutputBLabel);
                param->setHint(kParamOutputBHint);
                MultiPlane::Factory::addInputChannelOptionsRGBA(param, clipsForChannels, true /*addConstants*/, true /*onlyColorPlane*/);
                param->setDefault(eInputChannelAB);
                if (page) {
                    page->addChild(*param);
                }
            }
        }
    }
    // ouputA
    if (gSupportsRGBA || gSupportsAlpha) {
        if (gIsMultiPlanarV1 || gIsMultiPlanarV2) {
            ChoiceParamDescriptor* a = MultiPlane::Factory::describeInContextAddPlaneChannelChoice(desc, page, clipsForChannels, kParamOutputA, kParamOutputALabel, kParamOutputAHint);
            a->setDefault(eInputChannelAA);
        } else {
            {
                ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamOutputA);
                param->setLabel(kParamOutputALabel);
                param->setHint(kParamOutputAHint);
                MultiPlane::Factory::addInputChannelOptionsRGBA(param, clipsForChannels, true /*addConstants*/, true /*onlyColorPlane*/);
                param->setDefault(eInputChannelAA);
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
