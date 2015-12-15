 /* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2015 INRIA
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

#include "Shuffle.h"

#include <cmath>
#include <set>
#include <algorithm>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsPixelProcessor.h"
#include "ofxsMacros.h"
#include "ofxsCoords.h"

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

#define kEnableMultiPlanar true

#define kParamOutputComponents "outputComponents"
#define kParamOutputComponentsLabel "Output Components"
#define kParamOutputComponentsHint "Components in the output"
#define kParamOutputComponentsOptionRGBA "RGBA"
#define kParamOutputComponentsOptionRGB "RGB"
#define kParamOutputComponentsOptionAlpha "Alpha"
#ifdef OFX_EXTENSIONS_NATRON
#define kParamOutputComponentsOptionXY "XY"
#endif

#define kParamOutputChannels kNatronOfxParamOutputChannels
#define kParamOutputChannelsChoice kParamOutputChannels "Choice"
#define kParamOutputChannelsLabel "Output Layer"
#define kParamOutputChannelsHint "The layer that will be written to in output"


#define kParamOutputBitDepth "outputBitDepth"
#define kParamOutputBitDepthLabel "Output Bit Depth"
#define kParamOutputBitDepthHint "Bit depth of the output.\nWARNING: the conversion is linear, even for 8-bit or 16-bit depth. Use with care."
#define kParamOutputBitDepthOptionByte "Byte (8 bits)"
#define kParamOutputBitDepthOptionShort "Short (16 bits)"
#define kParamOutputBitDepthOptionFloat "Float (32 bits)"

#define kParamOutputR "outputR"
#define kParamOutputRChoice kParamOutputR "Choice"
#define kParamOutputRLabel "R"
#define kParamOutputRHint "Input channel for the output red channel"

#define kParamOutputG "outputG"
#define kParamOutputGChoice kParamOutputG "Choice"
#define kParamOutputGLabel "G"
#define kParamOutputGHint "Input channel for the output green channel"

#define kParamOutputB "outputB"
#define kParamOutputBChoice kParamOutputB "Choice"
#define kParamOutputBLabel "B"
#define kParamOutputBHint "Input channel for the output blue channel"


#define kParamCreateAlpha "createA"
#define kParamOutputAChoice kParamOutputA "Choice"
#define kParamCreateAlphaLabel "Create Alpha"
#define kParamCreateAlphaHint "When input stream is RGB, checking this will create an alpha filled with what is idendicated by the \"A\" parameter."

#define kParamOutputA "outputA"
#define kParamOutputALabel "A"
#define kParamOutputAHint "Input channel for the output alpha channel"

#define kParamOutputOptionAR "A.r"
#define kParamOutputOptionARHint "R channel from input A"
#define kParamOutputOptionAG "A.g"
#define kParamOutputOptionAGHint "G channel from input A"
#define kParamOutputOptionAB "A.b"
#define kParamOutputOptionABHint "B channel from input A"
#define kParamOutputOptionAA "A.a"
#define kParamOutputOptionAAHint "A channel from input A"
#define kParamOutputOption0 "0"
#define kParamOutputOption0Hint "0 constant channel"
#define kParamOutputOption1 "1"
#define kParamOutputOption1Hint "1 constant channel"
#define kParamOutputOptionBR "B.r"
#define kParamOutputOptionBRHint "R channel from input B"
#define kParamOutputOptionBG "B.g"
#define kParamOutputOptionBGHint "G channel from input B"
#define kParamOutputOptionBB "B.b"
#define kParamOutputOptionBBHint "B channel from input B"
#define kParamOutputOptionBA "B.a"
#define kParamOutputOptionBAHint "A channel from input B"

#define kShuffleColorAlpha "Alpha"
#define kShuffleColorRGB "RGB"
#define kShuffleColorRGBA "RGBA"
#define kShuffleMotionBackwardPlaneName "Backward"
#define kShuffleMotionForwardPlaneName "Forward"
#define kShuffleDisparityLeftPlaneName "DisparityLeft"
#define kShuffleDisparityRightPlaneName "DisparityRight"

#define kParamClipInfo "clipInfo"
#define kParamClipInfoLabel "Clip Info..."
#define kParamClipInfoHint "Display information about the inputs"

// TODO: sRGB/Rec.709 conversions for byte/short types

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

#define kClipA "A"
#define kClipB "B"

static bool gSupportsBytes  = false;
static bool gSupportsShorts = false;
static bool gSupportsFloats = false;
static bool gSupportsRGBA   = false;
static bool gSupportsRGB    = false;
static bool gSupportsAlpha  = false;
#ifdef OFX_EXTENSIONS_NATRON
static bool gSupportsXY     = false;
#endif
static bool gSupportsDynamicChoices = false;
static bool gIsMultiPlanar = false;

static OFX::PixelComponentEnum gOutputComponentsMap[5]; // 4 components + a sentinel at the end with ePixelComponentNone
static OFX::BitDepthEnum gOutputBitDepthMap[4]; // 3 possible bit depths + a sentinel

using namespace OFX;

class ShufflerBase : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImgA;
    const OFX::Image *_srcImgB;
    PixelComponentEnum _outputComponents;
    int _outputComponentCount;
    BitDepthEnum _outputBitDepth;
    std::vector<InputChannelEnum> _channelMap;

    public:
    ShufflerBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImgA(0)
    , _srcImgB(0)
    , _outputComponents(ePixelComponentNone)
    , _outputComponentCount(0)
    , _outputBitDepth(eBitDepthNone)
    , _channelMap()
    {
    }

    void setSrcImg(const OFX::Image *A, const OFX::Image *B) {_srcImgA = A; _srcImgB = B;}

    void setValues(PixelComponentEnum outputComponents,
                   int outputComponentCount,
                   BitDepthEnum outputBitDepth,
                   const std::vector<InputChannelEnum> &channelMap)
    {
        _outputComponents = outputComponents,
        _outputComponentCount = outputComponentCount,
        _outputBitDepth = outputBitDepth;
        assert(_outputComponentCount == (int)channelMap.size());
        _channelMap = channelMap;
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
    return value * (numvals-1) + 0.5f;
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
public:
    Shuffler(OFX::ImageEffect &instance)
    : ShufflerBase(instance)
    {
    }

private:
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        const OFX::Image* channelMapImg[nComponentsDst];
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
#ifdef OFX_EXTENSIONS_NATRON
            case OFX::ePixelComponentXY:
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
            const OFX::Image* srcImg = channelMapImg[c];
            int srcComp = channelMapComp[c];

            for (int y = procWindow.y1; y < procWindow.y2; y++) {
                if (_effect.abort()) {
                    break;
                }

                PIXDST *dstPix = (PIXDST *) _dstImg->getPixelAddress(procWindow.x1, y);

                for (int x = procWindow.x1; x < procWindow.x2; x++) {
                    PIXSRC *srcPix = (PIXSRC *)  (srcImg ? srcImg->getPixelAddress(x, y) : 0);
                    // if there is a srcImg but we are outside of its RoD, it should be considered black and transparent
                    dstPix[c] = srcImg ? convertPixelDepth<PIXSRC,PIXDST>(srcPix ? srcPix[srcComp] : 0) : convertPixelDepth<float,PIXDST>(srcComp);
                    dstPix += nComponentsDst;
                }
            }
        }
    }
};

struct InputPlaneChannel {
    OFX::Image* img;
    int channelIndex;
    bool fillZero;
    
    InputPlaneChannel() : img(0), channelIndex(-1), fillZero(true) {}
};

class MultiPlaneShufflerBase : public OFX::ImageProcessor
{
protected:
    
    int _outputComponentCount;
    BitDepthEnum _outputBitDepth;
    int _nComponentsDst;
    std::vector<InputPlaneChannel> _inputPlanes;

public:
    MultiPlaneShufflerBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
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
class MultiPlaneShuffler : public MultiPlaneShufflerBase
{
public:
    MultiPlaneShuffler(OFX::ImageEffect &instance)
    : MultiPlaneShufflerBase(instance)
    {
    }
    
private:
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        assert(_inputPlanes.size() == nComponentsDst);
        // now compute the transformed image, component by component
        for (int c = 0; c < nComponentsDst; ++c) {
            
            const OFX::Image* srcImg = _inputPlanes[c].img;
            int srcComp = _inputPlanes[c].channelIndex;
            if (!srcImg) {
                srcComp = _inputPlanes[c].fillZero ? 0. : 1.;
            }
            
            for (int y = procWindow.y1; y < procWindow.y2; y++) {
                if (_effect.abort()) {
                    break;
                }
                
                PIXDST *dstPix = (PIXDST *) _dstImg->getPixelAddress(procWindow.x1, y);
                
                for (int x = procWindow.x1; x < procWindow.x2; x++) {
                    PIXSRC *srcPix = (PIXSRC *)  (srcImg ? srcImg->getPixelAddress(x, y) : 0);
                    // if there is a srcImg but we are outside of its RoD, it should be considered black and transparent
                    dstPix[c] = srcImg ? convertPixelDepth<PIXSRC,PIXDST>(srcPix ? srcPix[srcComp] : 0) : convertPixelDepth<float,PIXDST>(srcComp);
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
public:
    /** @brief ctor */
    ShufflePlugin(OfxImageEffectHandle handle, OFX::ContextEnum context)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClipA(0)
    , _srcClipB(0)
    , _outputComponents(0)
    , _outputComponentsString(0)
    , _outputBitDepth(0)
    , _r(0)
    , _g(0)
    , _b(0)
    , _a(0)
    , _createAlpha(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (1 <= _dstClip->getPixelComponentCount() && _dstClip->getPixelComponentCount() <= 4));
        _srcClipA = fetchClip(context == eContextGeneral ? kClipA : kOfxImageEffectSimpleSourceClipName);
        assert(_srcClipA && (1 <= _srcClipA->getPixelComponentCount() && _srcClipA->getPixelComponentCount() <= 4));
        if (context == eContextGeneral) {
            _srcClipB = fetchClip(kClipB);
            assert(_srcClipB && (1 <= _srcClipB->getPixelComponentCount() && _srcClipB->getPixelComponentCount() <= 4));
        }
        if (gIsMultiPlanar) {
            _outputComponents = fetchChoiceParam(kParamOutputChannels);
        } else {
            _outputComponents = fetchChoiceParam(kParamOutputComponents);
        }
        if (getImageEffectHostDescription()->supportsMultipleClipDepths) {
            _outputBitDepth = fetchChoiceParam(kParamOutputBitDepth);
        }
        _r = fetchChoiceParam(kParamOutputR);
        _g = fetchChoiceParam(kParamOutputG);
        _b = fetchChoiceParam(kParamOutputB);
        _a = fetchChoiceParam(kParamOutputA);
        
        _createAlpha = fetchBooleanParam(kParamCreateAlpha);
        
        if (gSupportsDynamicChoices) {
            _outputComponentsString = fetchStringParam(kParamOutputChannelsChoice);
            _channelParamStrings[0] = fetchStringParam(kParamOutputRChoice);
            _channelParamStrings[1] = fetchStringParam(kParamOutputGChoice);
            _channelParamStrings[2] = fetchStringParam(kParamOutputBChoice);
            _channelParamStrings[3] = fetchStringParam(kParamOutputAChoice);
            
            //We need to restore the choice params because the host may not call getClipPreference if all clips are disconnected
            //e.g: this can be from a copy/paste issued from the user
            setChannelsFromStringParams(false);
        } else {
            _channelParamStrings[0] = _channelParamStrings[1] = _channelParamStrings[2] = _channelParamStrings[3] = 0;
        }
    }

private:
    
    void setChannelsFromRed(double time);
    
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /* override is identity */
    virtual bool isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;
    
    virtual void getClipComponents(const OFX::ClipComponentsArguments& args, OFX::ClipComponentsSetter& clipComponents) OVERRIDE FINAL;

    /** @brief get the clip preferences */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /* override changedParam */
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

private:
    
    bool isIdentityInternal(double time, OFX::Clip*& identityClip);
    
    bool getPlaneNeededForParam(double time,
                                const std::list<std::string>& aComponents,
                                const std::list<std::string>& bComponents,
                                OFX::ChoiceParam* param,
                                OFX::Clip** clip,
                                std::string* ofxPlane,
                                std::string* ofxComponents,
                                int* channelIndexInPlane,
                                bool* isCreatingAlpha) const;
    
    bool getPlaneNeededInOutput(const std::list<std::string>& components,
                                OFX::ChoiceParam* param,
                                std::string* ofxPlane,
                                std::string* ofxComponents) const;
    
    void buildChannelMenus(const std::list<std::string> &outputComponents);
    
    void enableComponents(OFX::PixelComponentEnum originalOutputComponents, OFX::PixelComponentEnum outputComponentsWithCreateAlpha);
    

    /* internal render function */
    template <class DSTPIX, int nComponentsDst>
    void renderInternalForDstBitDepth(const OFX::RenderArguments &args, OFX::BitDepthEnum srcBitDepth);

    template <int nComponentsDst>
    void renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum srcBitDepth, OFX::BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(ShufflerBase &, const OFX::RenderArguments &args);
    void setupAndProcessMultiPlane(MultiPlaneShufflerBase &, const OFX::RenderArguments &args);
    
    ///To be called once the components are known via getClipPreferences
    void setChannelsFromStringParams(bool allowReset);
    void setChannelsFromStringParamsInternal(const std::vector<std::string>& outputChoices,
                                             const std::vector<std::string>& rChoices,
                                             const std::vector<std::string>& gChoices,
                                             const std::vector<std::string>& bChoices,
                                             const std::vector<std::string>& aChoices,
                                             bool allowReset);

    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClipA;
    OFX::Clip *_srcClipB;

    OFX::ChoiceParam *_outputComponents;
    OFX::StringParam *_outputComponentsString;
    OFX::ChoiceParam *_outputBitDepth;
    OFX::ChoiceParam *_r;
    OFX::ChoiceParam *_g;
    OFX::ChoiceParam *_b;
    OFX::ChoiceParam *_a;
    OFX::StringParam* _channelParamStrings[4];
    OFX::BooleanParam *_createAlpha;
    
    //Small cache only used on main-thread to speed up getclipPreferences
    std::list<std::string> _currentOutputComps,_currentCompsA,_currentCompsB;
};




static void extractChannelsFromComponentString(const std::string& comp,
                                               std::string* layer,
                                               std::string* pairedLayer, //< if disparity or motion vectors
                                               std::vector<std::string>* channels)
{
    if (comp == kOfxImageComponentAlpha) {
        //*layer = kShuffleColorPlaneName;
        channels->push_back("A");
    } else if (comp == kOfxImageComponentRGB) {
        //*layer = kShuffleColorPlaneName;
        channels->push_back("R");
        channels->push_back("G");
        channels->push_back("B");
    } else if (comp == kOfxImageComponentRGBA) {
        //*layer = kShuffleColorPlaneName;
        channels->push_back("R");
        channels->push_back("G");
        channels->push_back("B");
        channels->push_back("A");
    } else if (comp == kFnOfxImageComponentMotionVectors) {
        *layer = kShuffleMotionBackwardPlaneName;
        *pairedLayer = kShuffleMotionForwardPlaneName;
        channels->push_back("U");
        channels->push_back("V");
    } else if (comp == kFnOfxImageComponentStereoDisparity) {
        *layer = kShuffleDisparityLeftPlaneName;
        *pairedLayer = kShuffleDisparityRightPlaneName;
        channels->push_back("X");
        channels->push_back("Y");
#ifdef OFX_EXTENSIONS_NATRON
    } else if (comp == kNatronOfxImageComponentXY) {
        channels->push_back("X");
        channels->push_back("Y");
    } else {
        std::vector<std::string> layerChannels = mapPixelComponentCustomToLayerChannels(comp);
        if (layerChannels.size() >= 1) {
            *layer = layerChannels[0];
            channels->assign(layerChannels.begin() + 1, layerChannels.end());
        }
#endif
    }
}

static void appendComponents(const std::string& clipName,
                             const std::list<std::string>& components,
                             OFX::ChoiceParam** params,
                             std::vector<std::string>* channelChoices)
{
    
    std::list<std::string> usedComps;
    for (std::list<std::string>::const_iterator it = components.begin(); it!=components.end(); ++it) {
        std::string layer, secondLayer;
        std::vector<std::string> channels;
        extractChannelsFromComponentString(*it, &layer, &secondLayer, &channels);
        if (channels.empty()) {
            continue;
        }
        if (layer.empty()) {
            continue;
        }
        for (std::size_t i = 0; i < channels.size(); ++i) {
            std::string opt = clipName + ".";
            if (!layer.empty()) {
                opt.append(layer);
                opt.push_back('.');
            }
            opt.append(channels[i]);
            
            if (std::find(usedComps.begin(), usedComps.end(), opt) == usedComps.end()) {
                usedComps.push_back(opt);
                for (int j = 0; j < 4; ++j) {
                    params[j]->appendOption(opt, channels[i] + " channel from " + ((layer.empty())? std::string() : std::string("layer/view ") + layer + " of ") + "input " + clipName);
                    if (channelChoices && j == 0) {
                        channelChoices->push_back(opt);
                    }
                }
            }
            
        }
        
        if (!secondLayer.empty()) {
            for (std::size_t i = 0; i < channels.size(); ++i) {
                std::string opt = clipName + ".";
                if (!secondLayer.empty()) {
                    opt.append(secondLayer);
                    opt.push_back('.');
                }
                opt.append(channels[i]);
                if (std::find(usedComps.begin(), usedComps.end(), opt) == usedComps.end()) {
                    usedComps.push_back(opt);
                    for (int j = 0; j < 4; ++j) {
                        params[j]->appendOption(opt, channels[i] + " channel from layer " + secondLayer + " of input " + clipName);
                        if (channelChoices && j == 0) {
                            channelChoices->push_back(opt);
                        }
                    }
                }
            }
        }
    }
}

template<typename T>
static void
addInputChannelOptionsRGBA(T* outputR, OFX::ContextEnum context, std::vector<std::string>* outputComponents)
{
    assert(outputR->getNOptions() == eInputChannelAR);
    outputR->appendOption(kParamOutputOptionAR,kParamOutputOptionARHint);
    if (outputComponents) {
        outputComponents->push_back(kParamOutputOptionAR);
    }
    assert(outputR->getNOptions() == eInputChannelAG);
    outputR->appendOption(kParamOutputOptionAG,kParamOutputOptionAGHint);
    if (outputComponents) {
        outputComponents->push_back(kParamOutputOptionAG);
    }
    assert(outputR->getNOptions() == eInputChannelAB);
    outputR->appendOption(kParamOutputOptionAB,kParamOutputOptionABHint);
    if (outputComponents) {
        outputComponents->push_back(kParamOutputOptionAB);
    }
    assert(outputR->getNOptions() == eInputChannelAA);
    outputR->appendOption(kParamOutputOptionAA,kParamOutputOptionAAHint);
    if (outputComponents) {
        outputComponents->push_back(kParamOutputOptionAA);
    }
    assert(outputR->getNOptions() == eInputChannel0);
    outputR->appendOption(kParamOutputOption0,kParamOutputOption0Hint);
    if (outputComponents) {
        outputComponents->push_back(kParamOutputOption0);
    }
    assert(outputR->getNOptions() == eInputChannel1);
    outputR->appendOption(kParamOutputOption1,kParamOutputOption1Hint);
    if (outputComponents) {
        outputComponents->push_back(kParamOutputOption1);
    }
    if (context == eContextGeneral) {
        assert(outputR->getNOptions() == eInputChannelBR);
        outputR->appendOption(kParamOutputOptionBR,kParamOutputOptionBRHint);
        if (outputComponents) {
            outputComponents->push_back(kParamOutputOptionBR);
        }
        assert(outputR->getNOptions() == eInputChannelBG);
        outputR->appendOption(kParamOutputOptionBG,kParamOutputOptionBGHint);
        if (outputComponents) {
            outputComponents->push_back(kParamOutputOptionBG);
        }
        assert(outputR->getNOptions() == eInputChannelBB);
        outputR->appendOption(kParamOutputOptionBB,kParamOutputOptionBBHint);
        if (outputComponents) {
            outputComponents->push_back(kParamOutputOptionBB);
        }
        assert(outputR->getNOptions() == eInputChannelBA);
        outputR->appendOption(kParamOutputOptionBA,kParamOutputOptionBAHint);
        if (outputComponents) {
            outputComponents->push_back(kParamOutputOptionBA);
        }
    }
}

static bool hasListChanged(const std::list<std::string>& oldList, const std::list<std::string>& newList)
{
    if (oldList.size() != newList.size()) {
        return true;
    }
    
    std::list<std::string>::const_iterator itNew = newList.begin();
    for (std::list<std::string>::const_iterator it = oldList.begin(); it!=oldList.end(); ++it,++itNew) {
        if (*it != *itNew) {
            return true;
        }
    }
    return false;
}

void
ShufflePlugin::buildChannelMenus(const std::list<std::string> &outputComponents)
{
    assert(gSupportsDynamicChoices);
    
    std::list<std::string> componentsA = _srcClipA->getComponentsPresent();
    std::list<std::string> componentsB = _srcClipB->getComponentsPresent();
    
    std::vector<std::string> channelChoices;
    if (hasListChanged(_currentCompsA, componentsA) ||
        hasListChanged(_currentCompsB, componentsB)) {
        
        _currentCompsA = componentsA;
        _currentCompsB = componentsB;
        
        _r->resetOptions();
        _g->resetOptions();
        _b->resetOptions();
        _a->resetOptions();
        
        OFX::ChoiceParam* params[4] = {_r, _g, _b, _a};
        
        //Always add RGBA channels for color plane
        for (int i = 0; i < 4; ++i) {
            addInputChannelOptionsRGBA(params[i], getContext(), i == 0 ? &channelChoices : 0);
        }
        
        if (gIsMultiPlanar) {
            appendComponents(kClipA, componentsA, params, &channelChoices);
            appendComponents(kClipB, componentsB, params, 0);
        }
    }
    
    if (gIsMultiPlanar) {
        
        if (hasListChanged(_currentOutputComps, outputComponents)) {
            
            _currentOutputComps = outputComponents;
            
            _outputComponents->resetOptions();
            
            std::vector<std::string> outputChoices;
            
            ///Pre-process to add color comps first
            std::list<std::string> compsToAdd;
            bool foundColor = false;
            for (std::list<std::string>::const_iterator it = outputComponents.begin(); it!=outputComponents.end(); ++it) {
                std::string layer, secondLayer;
                std::vector<std::string> channels;
                extractChannelsFromComponentString(*it, &layer, &secondLayer, &channels);
                if (channels.empty()) {
                    continue;
                }
                if (layer.empty()) {
                    if (*it == kOfxImageComponentRGBA) {
                        outputChoices.push_back(kShuffleColorRGBA);
                        foundColor = true;
                    } else if (*it == kOfxImageComponentRGB) {
                        outputChoices.push_back(kShuffleColorRGB);
                        foundColor = true;
                    } else if (*it == kOfxImageComponentAlpha) {
                        outputChoices.push_back(kShuffleColorAlpha);
                        foundColor = true;
                    }
                    
                    continue;
                } else {
                    if (layer == kShuffleMotionForwardPlaneName ||
                        layer == kShuffleMotionBackwardPlaneName ||
                        layer == kShuffleDisparityLeftPlaneName ||
                        layer == kShuffleDisparityRightPlaneName) {
                        continue;
                    }
                }
                compsToAdd.push_back(layer);
            }
            if (!foundColor) {
                outputChoices.push_back(kShuffleColorRGBA);
            }
            outputChoices.push_back(kShuffleMotionForwardPlaneName);
            outputChoices.push_back(kShuffleMotionBackwardPlaneName);
            outputChoices.push_back(kShuffleDisparityLeftPlaneName);
            outputChoices.push_back(kShuffleDisparityRightPlaneName);
            outputChoices.insert(outputChoices.end(), compsToAdd.begin(), compsToAdd.end());
            
            for (std::vector<std::string>::const_iterator it = outputChoices.begin(); it!=outputChoices.end(); ++it) {
                _outputComponents->appendOption(*it);
            }
            
            setChannelsFromStringParamsInternal(outputChoices, channelChoices, channelChoices, channelChoices, channelChoices,true);
        }
    }
}

bool
ShufflePlugin::getPlaneNeededForParam(double time,
                                      const std::list<std::string>& aComponents,
                                      const std::list<std::string>& bComponents,
                                      OFX::ChoiceParam* param,
                                      OFX::Clip** clip,
                                      std::string* ofxPlane,
                                      std::string* ofxComponents,
                                      int* channelIndexInPlane,
                                      bool* isCreatingAlpha) const
{
    assert(clip);
    *clip = 0;
    
    *isCreatingAlpha = false;
    
    int channelIndex;
    param->getValueAtTime(time, channelIndex);
    std::string channelEncoded;
    param->getOption(channelIndex, channelEncoded);
    if (channelEncoded.empty()) {
        return false;
    }
    
    if (channelEncoded == kParamOutputOption0) {
        *ofxComponents =  kParamOutputOption0;
        return true;
    }
    
    if (channelEncoded == kParamOutputOption1) {
        *ofxComponents = kParamOutputOption1;
        return true;
    }
    
    std::string clipName = kClipA;
    
    // Must be at least something like "A."
    if (channelEncoded.size() < clipName.size() + 1) {
        return false;
    }
    
    if (channelEncoded.substr(0,clipName.size()) == clipName) {
        *clip = _srcClipA;
    }
    
    if (!*clip) {
        clipName = kClipB;
        if (channelEncoded.substr(0,clipName.size()) == clipName) {
            *clip = _srcClipB;
        }
    }
    
    if (!*clip) {
        return false;
    }
    
    std::size_t lastDotPos = channelEncoded.find_last_of('.');
    if (lastDotPos == std::string::npos || lastDotPos == channelEncoded.size() - 1) {
        *clip = 0;
        return false;
    }
    
    std::string chanName = channelEncoded.substr(lastDotPos + 1,std::string::npos);
    std::string layerName;
    for (std::size_t i = clipName.size() + 1; i < lastDotPos; ++i) {
        layerName.push_back(channelEncoded[i]);
    }
    
    if (layerName.empty() ||
        layerName == kShuffleColorAlpha ||
        layerName == kShuffleColorRGB ||
        layerName == kShuffleColorRGBA) {
        std::string comp = (*clip)->getPixelComponentsProperty();
        if (chanName == "r" || chanName == "R" || chanName == "x" || chanName == "X") {
            *channelIndexInPlane = 0;
        } else if (chanName == "g" || chanName == "G" || chanName == "y" || chanName == "Y") {
            *channelIndexInPlane = 1;
        } else if (chanName == "b" || chanName == "B" || chanName == "z" || chanName == "Z") {
            *channelIndexInPlane = 2;
        } else if (chanName == "a" || chanName == "A" || chanName == "w" || chanName == "W") {
            if (comp == kOfxImageComponentAlpha) {
                *channelIndexInPlane = 0;
            } else if (comp == kOfxImageComponentRGBA) {
                *channelIndexInPlane = 3;
            } else {
                *isCreatingAlpha = true;
                *ofxComponents = kParamOutputOption1;
                return true;
            }
        } else {
            assert(false);
        }
        *ofxComponents = comp;
        *ofxPlane = kFnOfxImagePlaneColour;
        return true;
    } else if (layerName == kShuffleDisparityLeftPlaneName) {
        if (chanName == "x" || chanName == "X") {
            *channelIndexInPlane = 0;
        } else if (chanName == "y" || chanName == "Y") {
            *channelIndexInPlane = 1;
        } else {
            assert(false);
        }
        *ofxComponents = kFnOfxImageComponentStereoDisparity;
        *ofxPlane = kFnOfxImagePlaneStereoDisparityLeft;
        return true;
    } else if (layerName == kShuffleDisparityRightPlaneName) {
        if (chanName == "x" || chanName == "X") {
            *channelIndexInPlane = 0;
        } else if (chanName == "y" || chanName == "Y") {
            *channelIndexInPlane = 1;
        } else {
            assert(false);
        }
        *ofxComponents = kFnOfxImageComponentStereoDisparity;
        *ofxPlane =  kFnOfxImagePlaneStereoDisparityRight;
        return true;
    } else if (layerName == kShuffleMotionBackwardPlaneName) {
        if (chanName == "u" || chanName == "U") {
            *channelIndexInPlane = 0;
        } else if (chanName == "v" || chanName == "V") {
            *channelIndexInPlane = 1;
        } else {
            assert(false);
        }
        *ofxComponents = kFnOfxImageComponentMotionVectors;
        *ofxPlane = kFnOfxImagePlaneBackwardMotionVector;
        return true;
    } else if (layerName == kShuffleMotionForwardPlaneName) {
        if (chanName == "u" || chanName == "U") {
            *channelIndexInPlane = 0;
        } else if (chanName == "v" || chanName == "V") {
            *channelIndexInPlane = 1;
        } else {
            assert(false);
        }
        *ofxComponents = kFnOfxImageComponentMotionVectors;
        *ofxPlane = kFnOfxImagePlaneForwardMotionVector;
        return true;
#ifdef OFX_EXTENSIONS_NATRON
    } else {
        //Find in aComponents or bComponents a layer matching the name of the layer
        for (std::list<std::string>::const_iterator it = aComponents.begin(); it!=aComponents.end(); ++it) {
            //We found a matching layer
            std::string realLayerName;
            std::vector<std::string> channels;
            std::vector<std::string> layerChannels = mapPixelComponentCustomToLayerChannels(*it);
            if (layerChannels.empty() || layerName != layerChannels[0]) {
                // ignore it
                continue;
            }
            channels.assign(layerChannels.begin() + 1, layerChannels.end());
            int foundChannel = -1;
            for (std::size_t i = 0; i < channels.size(); ++i) {
                if (channels[i] == chanName) {
                    foundChannel = i;
                    break;
                }
            }
            assert(foundChannel != -1);
            *ofxPlane = *it;
            *channelIndexInPlane = foundChannel;
            *ofxComponents = *it;
            return true;
            
        }
        
        for (std::list<std::string>::const_iterator it = bComponents.begin(); it!=bComponents.end(); ++it) {
                //We found a matching layer
                std::string realLayerName;
                std::vector<std::string> channels;
                std::vector<std::string> layerChannels = mapPixelComponentCustomToLayerChannels(*it);
                if (layerChannels.empty()  || layerName != layerChannels[0]) {
                    // ignore it
                    continue;
                }
                channels.assign(layerChannels.begin() + 1, layerChannels.end());
                int foundChannel = -1;
                for (std::size_t i = 0; i < channels.size(); ++i) {
                    if (channels[i] == chanName) {
                        foundChannel = i;
                        break;
                    }
                }
                assert(foundChannel != -1);
                *ofxPlane = *it;
                *channelIndexInPlane = foundChannel;
                *ofxComponents = *it;
                return true;
            
        }
#endif // OFX_EXTENSIONS_NATRON
    }
    return false;
}


bool
ShufflePlugin::getPlaneNeededInOutput(const std::list<std::string>& components,
                                      OFX::ChoiceParam* param,
                                      std::string* ofxPlane,
                                      std::string* ofxComponents) const
{
    int layer_i;
    param->getValue(layer_i);
    std::string layerName;
    param->getOption(layer_i, layerName);
    
    if (layerName.empty() ||
        layerName == kShuffleColorRGBA ||
        layerName == kShuffleColorRGB ||
        layerName == kShuffleColorAlpha) {
        std::string comp = _dstClip->getPixelComponentsProperty();
        *ofxComponents = comp;
        *ofxPlane = kFnOfxImagePlaneColour;
        return true;
    } else if (layerName == kShuffleDisparityLeftPlaneName) {
        *ofxComponents = kFnOfxImageComponentStereoDisparity;
        *ofxPlane = kFnOfxImagePlaneStereoDisparityLeft;
        return true;
    } else if (layerName == kShuffleDisparityRightPlaneName) {
        *ofxComponents = kFnOfxImageComponentStereoDisparity;
        *ofxPlane =  kFnOfxImagePlaneStereoDisparityRight;
        return true;
    } else if (layerName == kShuffleMotionBackwardPlaneName) {
        *ofxComponents = kFnOfxImageComponentMotionVectors;
        *ofxPlane = kFnOfxImagePlaneBackwardMotionVector;
        return true;
    } else if (layerName == kShuffleMotionForwardPlaneName) {
        *ofxComponents = kFnOfxImageComponentMotionVectors;
        *ofxPlane = kFnOfxImagePlaneForwardMotionVector;
        return true;
#ifdef OFX_EXTENSIONS_NATRON
    } else {
        //Find in aComponents or bComponents a layer matching the name of the layer
        for (std::list<std::string>::const_iterator it = components.begin(); it!=components.end(); ++it) {
            if (it->find(layerName) != std::string::npos) {
                //We found a matching layer
                std::string realLayerName;
                std::vector<std::string> layerChannels = mapPixelComponentCustomToLayerChannels(*it);
                if (layerChannels.empty()) {
                    // ignore it
                    continue;
                }
                *ofxPlane = *it;
                *ofxComponents = *it;
                return true;
            }
        }
      #endif // OFX_EXTENSIONS_NATRON
    }
    return false;
}


void
ShufflePlugin::getClipComponents(const OFX::ClipComponentsArguments& args, OFX::ClipComponentsSetter& clipComponents)
{
    const double time = args.time;
    std::list<std::string> componentsA = _srcClipA->getComponentsPresent();
    std::list<std::string> componentsB = _srcClipB->getComponentsPresent();
    
    if (gIsMultiPlanar) {
        std::list<std::string> outputComponents = _dstClip->getComponentsPresent();
        std::string ofxPlane,ofxComp;
        getPlaneNeededInOutput(outputComponents, _outputComponents, &ofxPlane, &ofxComp);
        clipComponents.addClipComponents(*_dstClip, ofxComp);
    } else {
        int outputComponents_i;
        _outputComponents->getValueAtTime(time, outputComponents_i);
        PixelComponentEnum outputComponents = gOutputComponentsMap[outputComponents_i];
        clipComponents.addClipComponents(*_dstClip, outputComponents);
    }
    
    OFX::ChoiceParam* params[4] = {_r, _g, _b, _a};
    
    std::map<OFX::Clip*,std::set<std::string> > clipMap;
    
    bool isCreatingAlpha;
    for (int i = 0; i < 4; ++i) {
        std::string ofxComp,ofxPlane;
        int channelIndex;
        OFX::Clip* clip = 0;
        bool ok = getPlaneNeededForParam(time, componentsA, componentsB, params[i], &clip, &ofxPlane, &ofxComp, &channelIndex,&isCreatingAlpha);
        if (!ok) {
            continue;
        }
        if (ofxComp == kParamOutputOption0 || ofxComp == kParamOutputOption1) {
            continue;
        }
        assert(clip);
        
        std::map<OFX::Clip*,std::set<std::string> >::iterator foundClip = clipMap.find(clip);
        if (foundClip == clipMap.end()) {
            std::set<std::string> s;
            s.insert(ofxComp);
            clipMap.insert(std::make_pair(clip, s));
            clipComponents.addClipComponents(*clip, ofxComp);
        } else {
            std::pair<std::set<std::string>::iterator,bool> ret = foundClip->second.insert(ofxComp);
            if (ret.second) {
                clipComponents.addClipComponents(*clip, ofxComp);
            }
        }
    }
    
   
}

struct IdentityChoiceData
{
    OFX::Clip* clip;
    std::string components;
    int index;
};

bool
ShufflePlugin::isIdentityInternal(double time, OFX::Clip*& identityClip)
{
    if (!gSupportsDynamicChoices || !gIsMultiPlanar) {
        int r_i;
        _r->getValueAtTime(time, r_i);
        InputChannelEnum r = InputChannelEnum(r_i);
        int g_i;
        _g->getValueAtTime(time, g_i);
        InputChannelEnum g = InputChannelEnum(g_i);
        int b_i;
        _b->getValueAtTime(time, b_i);
        InputChannelEnum b = InputChannelEnum(b_i);
        int a_i;
        _a->getValueAtTime(time, a_i);
        InputChannelEnum a = InputChannelEnum(a_i);
        
        if (r == eInputChannelAR && g == eInputChannelAG && b == eInputChannelAB && a == eInputChannelAA && _srcClipA) {
            identityClip = _srcClipA;
            
            return true;
        }
        if (r == eInputChannelBR && g == eInputChannelBG && b == eInputChannelBB && a == eInputChannelBA && _srcClipB) {
            identityClip = _srcClipB;
            
            return true;
        }
        return false;
    } else {
        std::list<std::string> componentsA = _srcClipA->getComponentsPresent();
        std::list<std::string> componentsB = _srcClipB->getComponentsPresent();
        std::list<std::string> outputsComponents = _dstClip->getComponentsPresent();
        
        OFX::ChoiceParam* params[4] = {_r, _g, _b, _a};
        IdentityChoiceData data[4];
        
        
        std::string dstPlane,dstComponents;
        getPlaneNeededInOutput(outputsComponents, _outputComponents, &dstPlane, &dstComponents);
        if (dstPlane != kFnOfxImagePlaneColour) {
            return false;
        }
        
        int expectedIndex = -1;
        for (int i = 0; i < 4; ++i) {
            std::string plane;
            bool isCreatingAlpha;
            bool ok = getPlaneNeededForParam(time, componentsA, componentsB, params[i], &data[i].clip, &plane, &data[i].components, &data[i].index,&isCreatingAlpha);
            if (!ok) {
                //We might have an index in the param different from the actual components if getClipPreferences was not called so far
                return false;
            }
            if (plane != kFnOfxImagePlaneColour) {
                if (i != 3) {
                    //This is not the color plane, no identity
                    return false;
                } else {
                    //In this case if the A choice is visible, the user either checked "Create alpha" or he/she set it explicitly to 0 or 1
                    if (!_a->getIsSecret() &&! isCreatingAlpha) {
                        return false;
                    } else {
                        ///Do not do the checks below
                        continue;
                    }
                }
            }
            if (i > 0) {
                if (data[i].index != expectedIndex || data[i].components != data[0].components ||
                    data[i].clip != data[0].clip) {
                    return false;
                }
            }
            expectedIndex = data[i].index + 1;
        }
        identityClip = data[0].clip;
        return true;
    }
}

bool
ShufflePlugin::isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &/*identityTime*/)
{
    const double time = args.time;
    return isIdentityInternal(time, identityClip);
}

bool
ShufflePlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    const double time = args.time;
    OFX::Clip* identityClip = 0;
    if (isIdentityInternal(time, identityClip)) {
        rod = identityClip->getRegionOfDefinition(args.time);
        return true;
    }
    if (_srcClipA && _srcClipA->isConnected() && _srcClipB && _srcClipB->isConnected()) {
        OfxRectD rodA = _srcClipA->getRegionOfDefinition(args.time);
        OfxRectD rodB = _srcClipB->getRegionOfDefinition(args.time);
        OFX::Coords::rectBoundingBox(rodA, rodB, &rod);

        return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */




/* set up and run a processor */
void
ShufflePlugin::setupAndProcess(ShufflerBase &processor, const OFX::RenderArguments &args)
{
    std::auto_ptr<OFX::Image> dst(_dstClip->fetchImage(args.time));
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    const double time = args.time;
    OFX::BitDepthEnum         dstBitDepth    = dst->getPixelDepth();
    OFX::PixelComponentEnum   dstComponents  = dst->getPixelComponents();
    if (dstBitDepth != _dstClip->getPixelDepth() ||
        dstComponents != _dstClip->getPixelComponents()) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dst->getRenderScale().x != args.renderScale.x ||
        dst->getRenderScale().y != args.renderScale.y ||
        (dst->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && dst->getField() != args.fieldToRender)) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    
    
    InputChannelEnum r,g,b,a;
    // compute the components mapping tables
    std::vector<InputChannelEnum> channelMap;
    
    std::auto_ptr<const OFX::Image> srcA((_srcClipA && _srcClipA->isConnected()) ?
                                         _srcClipA->fetchImage(args.time) : 0);
    std::auto_ptr<const OFX::Image> srcB((_srcClipB && _srcClipB->isConnected()) ?
                                         _srcClipB->fetchImage(args.time) : 0);
    OFX::BitDepthEnum srcBitDepth = eBitDepthNone;
    OFX::PixelComponentEnum srcComponents = ePixelComponentNone;
    if (srcA.get()) {
        if (srcA->getRenderScale().x != args.renderScale.x ||
            srcA->getRenderScale().y != args.renderScale.y ||
            (srcA->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && srcA->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        srcBitDepth      = srcA->getPixelDepth();
        srcComponents = srcA->getPixelComponents();
        assert(_srcClipA->getPixelComponents() == srcComponents);
    }
    
    if (srcB.get()) {
        if (srcB->getRenderScale().x != args.renderScale.x ||
            srcB->getRenderScale().y != args.renderScale.y ||
            (srcB->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && srcB->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum    srcBBitDepth      = srcB->getPixelDepth();
        OFX::PixelComponentEnum srcBComponents = srcB->getPixelComponents();
        assert(_srcClipB->getPixelComponents() == srcBComponents);
        // both input must have the same bit depth and components
        if ((srcBitDepth != eBitDepthNone && srcBitDepth != srcBBitDepth) ||
            (srcComponents != ePixelComponentNone && srcComponents != srcBComponents)) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    
    int r_i;
    _r->getValueAtTime(time, r_i);
    r = InputChannelEnum(r_i);
    int g_i;
    _g->getValueAtTime(time, g_i);
    g = InputChannelEnum(g_i);
    int b_i;
    _b->getValueAtTime(time, b_i);
    b = InputChannelEnum(b_i);
    int a_i;
    _a->getValueAtTime(time, a_i);
    a = InputChannelEnum(a_i);
    
    
    switch (dstComponents) {
        case OFX::ePixelComponentRGBA:
            channelMap.resize(4);
            channelMap[0] = r;
            channelMap[1] = g;
            channelMap[2] = b;
            channelMap[3] = a;
            break;
        case OFX::ePixelComponentXY:
            channelMap.resize(2);
            channelMap[0] = r;
            channelMap[1] = g;
            break;
        case OFX::ePixelComponentRGB:
            channelMap.resize(3);
            channelMap[0] = r;
            channelMap[1] = g;
            channelMap[2] = b;
            break;
        case OFX::ePixelComponentAlpha:
            channelMap.resize(1);
            channelMap[0] = a;
            break;
        default:
            channelMap.resize(0);
            break;
    }
    processor.setSrcImg(srcA.get(),srcB.get());
    
    int outputComponents_i;
    _outputComponents->getValueAtTime(time, outputComponents_i);
    PixelComponentEnum outputComponents = gOutputComponentsMap[outputComponents_i];
    assert(dstComponents == outputComponents);
    BitDepthEnum outputBitDepth = srcBitDepth;
    if (getImageEffectHostDescription()->supportsMultipleClipDepths) {
        int outputBitDepth_i;
        _outputBitDepth->getValueAtTime(time, outputBitDepth_i);
        outputBitDepth = gOutputBitDepthMap[outputBitDepth_i];
    }
    assert(outputBitDepth == dstBitDepth);
    int outputComponentCount = dst->getPixelComponentCount();

    processor.setValues(outputComponents, outputComponentCount, outputBitDepth, channelMap);
    
    processor.setDstImg(dst.get());
    processor.setRenderWindow(args.renderWindow);

    processor.process();
}

class InputImagesHolder_RAII
{
    std::vector<OFX::Image*> images;
public:
    
    InputImagesHolder_RAII()
    : images()
    {
        
    }
    
    void appendImage(OFX::Image* img)
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
ShufflePlugin::setupAndProcessMultiPlane(MultiPlaneShufflerBase & processor, const OFX::RenderArguments &args)
{
    const double time = args.time;
    std::string dstOfxPlane,dstOfxComp;
    std::list<std::string> outputComponents = _dstClip->getComponentsPresent();
    getPlaneNeededInOutput(outputComponents, _outputComponents, &dstOfxPlane, &dstOfxComp);
    
#ifdef DEBUG
    // Follow the OpenFX spec:
    // check that dstComponents is consistent with the result of getClipPreferences
    // (@see getClipPreferences).
    OFX::PixelComponentEnum pixelComps = mapStrToPixelComponentEnum(dstOfxComp);
    OFX::PixelComponentEnum dstClipComps = _dstClip->getPixelComponents();
    if (pixelComps != OFX::ePixelComponentCustom) {
        assert(dstClipComps == pixelComps);
    } else {
        int nComps = std::max((int)mapPixelComponentCustomToLayerChannels(dstOfxComp).size() - 1, 0);
        switch (nComps) {
            case 1:
                pixelComps = OFX::ePixelComponentAlpha;
                break;
            case 2:
                pixelComps = OFX::ePixelComponentXY;
                break;
            case 3:
                pixelComps = OFX::ePixelComponentRGB;
                break;
            case 4:
                pixelComps = OFX::ePixelComponentRGBA;
            default:
                break;
        }
        assert(dstClipComps == pixelComps);
    }
#endif
    
    std::auto_ptr<OFX::Image> dst(_dstClip->fetchImagePlane(args.time, args.renderView, dstOfxPlane.c_str()));
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum         dstBitDepth    = dst->getPixelDepth();
    int nDstComponents = dst->getPixelComponentCount();
    if (dstBitDepth != _dstClip->getPixelDepth() ||
        nDstComponents != _dstClip->getPixelComponentCount()) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dst->getRenderScale().x != args.renderScale.x ||
        dst->getRenderScale().y != args.renderScale.y ||
        (dst->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && dst->getField() != args.fieldToRender)) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    
    

    
    std::list<std::string> componentsA = _srcClipA->getComponentsPresent();
    std::list<std::string> componentsB = _srcClipB->getComponentsPresent();
    
    OFX::ChoiceParam* params[4] = {_r, _g, _b, _a};
    
    InputImagesHolder_RAII imagesHolder;
    OFX::BitDepthEnum srcBitDepth = eBitDepthNone;
    
    std::map<OFX::Clip*,std::map<std::string,OFX::Image*> > fetchedPlanes;
    
    std::vector<InputPlaneChannel> planes;
    bool isCreatingAlpha;
    for (int i = 0; i < nDstComponents; ++i) {
        
        InputPlaneChannel p;
        OFX::Clip* clip = 0;
        std::string plane,ofxComp;
        bool ok = getPlaneNeededForParam(time,
                                         componentsA, componentsB,
                                         nDstComponents == 1 ? params[3] : params[i],
                                         &clip, &plane, &ofxComp, &p.channelIndex,&isCreatingAlpha);
        if (!ok) {
            setPersistentMessage(OFX::Message::eMessageError, "", "Cannot find requested channels in input");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        
        p.img = 0;
        if (ofxComp == kParamOutputOption0) {
            p.fillZero = true;
        } else if (ofxComp == kParamOutputOption1) {
            p.fillZero = false;
        } else {
            std::map<std::string,OFX::Image*>& clipPlanes = fetchedPlanes[clip];
            std::map<std::string,OFX::Image*>::iterator foundPlane = clipPlanes.find(plane);
            if (foundPlane != clipPlanes.end()) {
                p.img = foundPlane->second;
            } else {
                p.img = clip->fetchImagePlane(args.time, args.renderView, plane.c_str());
                if (p.img) {
                    clipPlanes.insert(std::make_pair(plane, p.img));
                    imagesHolder.appendImage(p.img);
                }

            }
        }
        
        if (p.img) {
            
            if (p.img->getRenderScale().x != args.renderScale.x ||
                p.img->getRenderScale().y != args.renderScale.y ||
                (p.img->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && p.img->getField() != args.fieldToRender)) {
                setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                OFX::throwSuiteStatusException(kOfxStatFailed);
            }
            if (srcBitDepth == eBitDepthNone) {
                srcBitDepth = p.img->getPixelDepth();
            } else {
                // both input must have the same bit depth and components
                if (srcBitDepth != eBitDepthNone && srcBitDepth != p.img->getPixelDepth()) {
                    OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
                }
            }
        }
        planes.push_back(p);
    }
    
    BitDepthEnum outputBitDepth = srcBitDepth;
    if (getImageEffectHostDescription()->supportsMultipleClipDepths) {
        int outputBitDepth_i;
        _outputBitDepth->getValueAtTime(time, outputBitDepth_i);
        outputBitDepth = gOutputBitDepthMap[outputBitDepth_i];
    }
    assert(outputBitDepth == dstBitDepth);

    processor.setValues(nDstComponents, outputBitDepth, planes);

    processor.setDstImg(dst.get());
    processor.setRenderWindow(args.renderWindow);
    
    processor.process();
}

template <class DSTPIX, int nComponentsDst>
void
ShufflePlugin::renderInternalForDstBitDepth(const OFX::RenderArguments &args, OFX::BitDepthEnum srcBitDepth)
{
    if (!gIsMultiPlanar || !gSupportsDynamicChoices) {
        switch (srcBitDepth) {
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
    } else {
        switch (srcBitDepth) {
            case OFX::eBitDepthUByte : {
                MultiPlaneShuffler<unsigned char, DSTPIX, nComponentsDst> fred(*this);
                setupAndProcessMultiPlane(fred, args);
            }
                break;
            case OFX::eBitDepthUShort : {
                MultiPlaneShuffler<unsigned short, DSTPIX, nComponentsDst> fred(*this);
                setupAndProcessMultiPlane(fred, args);
            }
                break;
            case OFX::eBitDepthFloat : {
                MultiPlaneShuffler<float, DSTPIX, nComponentsDst> fred(*this);
                setupAndProcessMultiPlane(fred, args);
            }
                break;
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }

    }
}

// the internal render function
template <int nComponentsDst>
void
ShufflePlugin::renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum srcBitDepth, OFX::BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
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
    assert (_srcClipA && _srcClipB && _dstClip);
    if (!_srcClipA || !_srcClipB || !_dstClip) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    const double time = args.time;
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();
#ifdef DEBUG
    // Follow the OpenFX spec:
    // check that dstComponents is consistent with the result of getClipPreferences
    // (@see getClipPreferences).
    if (gIsMultiPlanar && gSupportsDynamicChoices) {
        std::list<std::string> outputComponents = _dstClip->getComponentsPresent();
        std::string ofxPlane,ofxComponents;
        getPlaneNeededInOutput(outputComponents, _outputComponents, &ofxPlane, &ofxComponents);

        OFX::PixelComponentEnum pixelComps = mapStrToPixelComponentEnum(ofxComponents);
        if (pixelComps == OFX::ePixelComponentCustom) {
            int nComps = std::max((int)mapPixelComponentCustomToLayerChannels(ofxComponents).size() - 1, 0);
            switch (nComps) {
                case 1:
                    pixelComps = OFX::ePixelComponentAlpha;
                    break;
                case 2:
                    pixelComps = OFX::ePixelComponentXY;
                    break;
                case 3:
                    pixelComps = OFX::ePixelComponentRGB;
                    break;
                case 4:
                    pixelComps = OFX::ePixelComponentRGBA;
                default:
                    break;
            }
        }
        assert(dstComponents == pixelComps);
    } else {
        // set the components of _dstClip
        int outputComponents_i;
        _outputComponents->getValueAtTime(time, outputComponents_i);
        PixelComponentEnum outputComponents = gOutputComponentsMap[outputComponents_i];
        assert(dstComponents == outputComponents);
    }

    if (getImageEffectHostDescription()->supportsMultipleClipDepths) {
        // set the bitDepth of _dstClip
        int outputBitDepth_i;
        _outputBitDepth->getValueAtTime(time, outputBitDepth_i);
        BitDepthEnum outputBitDepth = gOutputBitDepthMap[outputBitDepth_i];
        assert(dstBitDepth == outputBitDepth);
    }
#endif
    int dstComponentCount  = _dstClip->getPixelComponentCount();
    assert(1 <= dstComponentCount && dstComponentCount <= 4);

    assert(kSupportsMultipleClipPARs   || _srcClipA->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || _srcClipA->getPixelDepth()       == _dstClip->getPixelDepth());
    assert(kSupportsMultipleClipPARs   || _srcClipB->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || _srcClipB->getPixelDepth()       == _dstClip->getPixelDepth());
    // get the components of _dstClip
    
    if (!gIsMultiPlanar) {
        int outputComponents_i;
        _outputComponents->getValueAtTime(time, outputComponents_i);
        PixelComponentEnum outputComponents = gOutputComponentsMap[outputComponents_i];
        if (dstComponents != outputComponents) {
            setPersistentMessage(OFX::Message::eMessageError, "", "Shuffle: OFX Host did not take into account output components");
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    
    if (getImageEffectHostDescription()->supportsMultipleClipDepths) {
        // get the bitDepth of _dstClip
        int outputBitDepth_i;
        _outputBitDepth->getValueAtTime(time, outputBitDepth_i);
        BitDepthEnum outputBitDepth = gOutputBitDepthMap[outputBitDepth_i];
        if (dstBitDepth != outputBitDepth) {
            setPersistentMessage(OFX::Message::eMessageError, "", "Shuffle: OFX Host did not take into account output bit depth");
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    OFX::BitDepthEnum srcBitDepth = _srcClipA->getPixelDepth();

    if (_srcClipA->isConnected() && _srcClipB->isConnected()) {
        OFX::BitDepthEnum srcBBitDepth = _srcClipB->getPixelDepth();
        // both input must have the same bit depth
        if (srcBitDepth != srcBBitDepth) {
            setPersistentMessage(OFX::Message::eMessageError, "", "Shuffle: both inputs must have the same bit depth");
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
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
}


/* Override the clip preferences */
void
ShufflePlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
    PixelComponentEnum originalDstPixelComps = OFX::ePixelComponentNone;
    PixelComponentEnum dstPixelComps = OFX::ePixelComponentNone;
    if (gIsMultiPlanar && gSupportsDynamicChoices) {
        std::list<std::string> outputComponents = _dstClip->getComponentsPresent();
        buildChannelMenus(outputComponents);
        std::string ofxPlane,ofxComponents;
        getPlaneNeededInOutput(outputComponents, _outputComponents, &ofxPlane, &ofxComponents);
        
        dstPixelComps = mapStrToPixelComponentEnum(ofxComponents);
        originalDstPixelComps = dstPixelComps;
        if (dstPixelComps == OFX::ePixelComponentCustom) {
            int nComps = std::max((int)mapPixelComponentCustomToLayerChannels(ofxComponents).size() - 1, 0);
            switch (nComps) {
                case 1:
                    dstPixelComps = OFX::ePixelComponentAlpha;
                    break;
                case 2:
                    dstPixelComps = OFX::ePixelComponentXY;
                    break;
                case 3:
                    dstPixelComps = OFX::ePixelComponentRGB;
                    break;
                case 4:
                    dstPixelComps = OFX::ePixelComponentRGBA;
                default:
                    break;
            }
        } else if (dstPixelComps == OFX::ePixelComponentRGB) {
            bool createAlpha;
            _createAlpha->getValue(createAlpha);
            if (createAlpha) {
                dstPixelComps = OFX::ePixelComponentRGBA;
            }
        }
    } else {
        // set the components of _dstClip
        int outputComponents_i;
        _outputComponents->getValue(outputComponents_i);
        dstPixelComps = gOutputComponentsMap[outputComponents_i];
        originalDstPixelComps = dstPixelComps;
    }
    
    clipPreferences.setClipComponents(*_dstClip, dstPixelComps);

    
    //Enable components according to the new dstPixelComps
    enableComponents(originalDstPixelComps,dstPixelComps);

    if (getImageEffectHostDescription()->supportsMultipleClipDepths) {
        // set the bitDepth of _dstClip
        int outputBitDepth_i;
        _outputBitDepth->getValue(outputBitDepth_i);
        BitDepthEnum outputBitDepth = gOutputBitDepthMap[outputBitDepth_i];
        clipPreferences.setClipBitDepth(*_dstClip, outputBitDepth);
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
#ifdef OFX_EXTENSIONS_NUKE
        case OFX::ePixelComponentMotionVectors:
            s += "MotionVectors";
            break;
        case OFX::ePixelComponentStereoDisparity:
            s += "StereoDisparity";
            break;
#endif
#ifdef OFX_EXTENSIONS_NATRON
        case OFX::ePixelComponentXY:
            s += "XY";
            break;
#endif
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
        case OFX::eBitDepthNone:
            s += "0";
            break;
#ifdef OFX_EXTENSIONS_VEGAS
        case OFX::eBitDepthUByteBGRA:
            s += "8uBGRA";
            break;
        case OFX::eBitDepthUShortBGRA:
            s += "16uBGRA";
            break;
        case OFX::eBitDepthFloatBGRA:
            s += "32fBGRA";
            break;
#endif
        default:
            s += "[unknown bit depth]";
            break;
    }
    return s;
}

static bool endsWith(const std::string &str, const std::string &suffix)
{
    return ((str.size() >= suffix.size()) &&
            (str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0));
}

void
ShufflePlugin::setChannelsFromRed(double time)
{
    int r_i;
    _r->getValueAtTime(time, r_i);
    std::string rChannel;
    _r->getOption(r_i, rChannel);
    
    if (endsWith(rChannel, ".R") || endsWith(rChannel, ".r")) {
        std::string base = rChannel.substr(0,rChannel.size() - 2);
        
        bool gSet = false;
        bool bSet = false;
        bool aSet = false;
        
        int nOpt = _g->getNOptions();
        
        int indexOf0 = -1;
        int indexOf1 = -1;
        
        for (int i = 0; i < nOpt; ++i) {
            std::string opt;
            _r->getOption(i, opt);
            
            if (opt == kParamOutputOption0) {
                indexOf0 = i;
            } else if (opt == kParamOutputOption1) {
                indexOf1 = i;
            } else if (opt.substr(0,base.size()) == base) {
                std::string chan = opt.substr(base.size());
                if (chan == ".G" || chan == ".g") {
                    _g->setValue(i);
                    if (_channelParamStrings[1]) {
                        _channelParamStrings[1]->setValue(opt);
                    }
                    gSet = true;
                } else if (chan == ".B" || chan == ".b") {
                    _b->setValue(i);
                    if (_channelParamStrings[2]) {
                        _channelParamStrings[2]->setValue(opt);
                    }
                    bSet = true;
                } else if (chan == ".A" || chan == ".a") {
                    _a->setValue(i);
                    if (_channelParamStrings[3]) {
                        _channelParamStrings[3]->setValue(opt);
                    }
                    aSet = true;
                }
            }
            if (gSet && bSet && aSet && indexOf0 != -1 && indexOf1 != -1) {
                // we're done
                break;
            }
        }
        assert(indexOf0 != -1 && indexOf1 != -1);
        if (!gSet) {
            _g->setValue(indexOf0);
            if (_channelParamStrings[1]) {
                _channelParamStrings[1]->setValue(kParamOutputOption0);
            }
        }
        if (!bSet) {
            _b->setValue(indexOf0);
            if (_channelParamStrings[2]) {
                _channelParamStrings[2]->setValue(kParamOutputOption0);
            }
        }
        if (!aSet) {
            _a->setValue(indexOf1);
            if (_channelParamStrings[3]) {
                _channelParamStrings[3]->setValue(kParamOutputOption0);
            }
        }
    }
}

void
ShufflePlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    //Commented out as it cannot be done here: enableComponents() relies on the clip components but the clip
    //components might not yet be set if the user changed a clip pref slaved param. Instead we have to move it into the getClipPreferences
    /*if (paramName == kParamOutputComponents || paramName == kParamOutputChannels) {
        enableComponents();
    } else*/ if (paramName == kParamClipInfo && args.reason == eChangeUserEdit) {
        std::string msg;
        msg += "Input A: ";
        if (!_srcClipA) {
            msg += "N/A";
        } else {
            msg += imageFormatString(_srcClipA->getPixelComponents(), _srcClipA->getPixelDepth());
        }
        msg += "\n";
        if (getContext() == eContextGeneral) {
            msg += "Input B: ";
            if (!_srcClipB) {
                msg += "N/A";
            } else {
                msg += imageFormatString(_srcClipB->getPixelComponents(), _srcClipB->getPixelDepth());
            }
            msg += "\n";
        }
        msg += "Output: ";
        if (!_dstClip) {
            msg += "N/A";
        } else {
            msg += imageFormatString(_dstClip->getPixelComponents(), _dstClip->getPixelDepth());
        }
        msg += "\n";
        sendMessage(OFX::Message::eMessageMessage, "", msg);
    } else if (paramName == kParamOutputR && args.reason == OFX::eChangeUserEdit && _channelParamStrings[0]) {
#ifdef OFX_EXTENSIONS_NATRON
        setChannelsFromRed(args.time);
#endif
        assert(_r);
        int choice_i;
        _r->getValueAtTime(args.time, choice_i);
        std::string optionName;
        _r->getOption(choice_i, optionName);
        _channelParamStrings[0]->setValue(optionName);
    } else if (paramName == kParamOutputG && args.reason == OFX::eChangeUserEdit && _channelParamStrings[1]) {
        assert(_g);
        int choice_i;
        _g->getValueAtTime(args.time, choice_i);
        std::string optionName;
        _g->getOption(choice_i, optionName);
        _channelParamStrings[1]->setValue(optionName);
    } else if (paramName == kParamOutputB && args.reason == OFX::eChangeUserEdit && _channelParamStrings[2]) {
        assert(_b);
        int choice_i;
        _b->getValueAtTime(args.time, choice_i);
        std::string optionName;
        _b->getOption(choice_i, optionName);
        _channelParamStrings[2]->setValue(optionName);
    } else if (paramName == kParamOutputA && args.reason == OFX::eChangeUserEdit && _channelParamStrings[3]) {
        assert(_a);
        int choice_i;
        _a->getValueAtTime(args.time, choice_i);
        std::string optionName;
        _a->getOption(choice_i, optionName);
        _channelParamStrings[3]->setValue(optionName);
    } else if (paramName == kParamOutputChannels && args.reason == OFX::eChangeUserEdit && _outputComponentsString) {
        int choice_i;
        _outputComponents->getValueAtTime(args.time, choice_i);
        std::string optionName;
        _outputComponents->getOption(choice_i, optionName);
        _outputComponentsString->setValue(optionName);
    }
    
}

void
ShufflePlugin::setChannelsFromStringParamsInternal(const std::vector<std::string>& outputChoices,
                                                   const std::vector<std::string>& rChoices,
                                                   const std::vector<std::string>& gChoices,
                                                   const std::vector<std::string>& bChoices,
                                                   const std::vector<std::string>& aChoices,
                                                   bool allowReset)
{
    if (!gSupportsDynamicChoices) {
        return;
    }
    std::string outputComponentsStr;
    _outputComponentsString->getValue(outputComponentsStr);
    if (outputComponentsStr.empty()) {
        int cur_i;
        _outputComponents->getValue(cur_i);
        if (cur_i >= 0 && cur_i < (int)outputChoices.size()) {
            outputComponentsStr = outputChoices[cur_i];
        }
        _outputComponents->getOption(cur_i, outputComponentsStr);
        _outputComponentsString->setValue(outputComponentsStr);
    } else {
        int foundOption = -1;
        for (int i = 0; i < (int)outputChoices.size(); ++i) {
            if (outputChoices[i] == outputComponentsStr) {
                foundOption = i;
                break;
            }
        }
        if (foundOption != -1) {
            _outputComponents->setValue(foundOption);
        } else {
            if (allowReset) {
                _outputComponents->setValue(0);
                _outputComponentsString->setValue(outputChoices[0]);
            }
        }
    }
    
    
    OFX::ChoiceParam* choiceParams[4] = {_r, _g, _b, _a};
    const std::vector<std::string>* channelOptions[4] = {&rChoices, &gChoices, &bChoices, &aChoices};
    
    for (int c = 0; c < 4; ++c) {
        std::string valueStr;
        _channelParamStrings[c]->getValue(valueStr);
        if (valueStr.empty()) {
            int cur_i;
            choiceParams[c]->getValue(cur_i);
            if (cur_i >= 0 && cur_i < (int)channelOptions[c]->size()) {
                valueStr = channelOptions[c]->at(cur_i);
            }
            _channelParamStrings[c]->setValue(valueStr);
        } else {
            int foundOption = -1;
            for (int i = 0; i < (int)channelOptions[c]->size(); ++i) {
                if (channelOptions[c]->at(i) == valueStr) {
                    foundOption = i;
                    break;
                }
            }
            if (foundOption != -1) {
                choiceParams[c]->setValue(foundOption);
            } else {
                if (allowReset) {
                    choiceParams[c]->setValue(c);
                    _channelParamStrings[c]->setValue(channelOptions[c]->at(c));
                }
            }
        }
    }

}

void
ShufflePlugin::setChannelsFromStringParams(bool allowReset)
{
    if (!gSupportsDynamicChoices) {
        return;
    }
    int nOpt = _outputComponents->getNOptions();
    std::vector<std::string> outputComponentsVec(nOpt);
    for (int i = 0; i < nOpt; ++i) {
        _outputComponents->getOption(i, outputComponentsVec[i]);
    }

    nOpt = _r->getNOptions();
    std::vector<std::string> rComps(nOpt);
    for (int i = 0; i < nOpt; ++i) {
        _r->getOption(i, rComps[i]);
    }
    
    nOpt = _g->getNOptions();
    std::vector<std::string> gComps(nOpt);
    for (int i = 0; i < nOpt; ++i) {
        _g->getOption(i, gComps[i]);
    }
    
    nOpt = _b->getNOptions();
    std::vector<std::string> bComps(nOpt);
    for (int i = 0; i < nOpt; ++i) {
        _b->getOption(i, bComps[i]);
    }
    
    nOpt = _a->getNOptions();
    std::vector<std::string> aComps(nOpt);
    for (int i = 0; i < nOpt; ++i) {
        _a->getOption(i, aComps[i]);
    }
    setChannelsFromStringParamsInternal(outputComponentsVec, rComps, gComps, bComps, aComps,allowReset);
}

void
ShufflePlugin::changedClip(const InstanceChangedArgs &/*args*/, const std::string &clipName)
{
    if (getContext() == eContextGeneral &&
        (clipName == kClipA || clipName == kClipB)) {
        // check that A and B are compatible if they're both connected
        if (_srcClipA && _srcClipA->isConnected() && _srcClipB && _srcClipB->isConnected()) {
            OFX::BitDepthEnum srcABitDepth = _srcClipA->getPixelDepth();
            OFX::BitDepthEnum srcBBitDepth = _srcClipB->getPixelDepth();
            // both input must have the same bit depth
            if (srcABitDepth != srcBBitDepth) {
                setPersistentMessage(OFX::Message::eMessageError, "", "Shuffle: both inputs must have the same bit depth");
                OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
            }
        }
    }
}


void
ShufflePlugin::enableComponents(PixelComponentEnum originalOutputComponents, PixelComponentEnum outputComponentsWithCreateAlpha)
{
    if (!gIsMultiPlanar) {
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
#ifdef OFX_EXTENSIONS_NUKE
            case ePixelComponentMotionVectors:
            case ePixelComponentStereoDisparity:
                _r->setEnabled(true);
                _g->setEnabled(true);
                _b->setEnabled(false);
                _a->setEnabled(false);
                break;
#endif
#ifdef OFX_EXTENSIONS_NATRON
            case ePixelComponentXY:
                _r->setEnabled(true);
                _g->setEnabled(true);
                _b->setEnabled(false);
                _a->setEnabled(false);
                break;
#endif
            default:
                assert(0);
                break;
        }
    } else { // if (!gIsMultiPlanar) {
        std::list<std::string> components =  _dstClip->getComponentsPresent();
        
        std::string ofxPlane,ofxComp;
        getPlaneNeededInOutput(components, _outputComponents, &ofxPlane, &ofxComp);
        std::vector<std::string> compNames;
        
        
        bool showCreateAlpha = false;
        if (ofxPlane == kFnOfxImagePlaneColour) {
            //std::string comp = _dstClip->getPixelComponentsProperty();
            if (outputComponentsWithCreateAlpha == OFX::ePixelComponentRGB) {
                compNames.push_back("R");
                compNames.push_back("G");
                compNames.push_back("B");
                showCreateAlpha = true;
            } else if (outputComponentsWithCreateAlpha == OFX::ePixelComponentRGBA) {
                compNames.push_back("R");
                compNames.push_back("G");
                compNames.push_back("B");
                compNames.push_back("A");
                
                if (originalOutputComponents != OFX::ePixelComponentRGBA) {
                    showCreateAlpha = true;
                }
            } else if (outputComponentsWithCreateAlpha == OFX::ePixelComponentAlpha) {
                compNames.push_back("Alpha");
            }

        } else if (ofxComp == kFnOfxImageComponentStereoDisparity) {
            compNames.push_back("X");
            compNames.push_back("Y");
        } else if (ofxComp == kFnOfxImageComponentMotionVectors) {
            compNames.push_back("U");
            compNames.push_back("V");
#ifdef OFX_EXTENSIONS_NATRON
        } else {
            std::vector<std::string> layerChannels = mapPixelComponentCustomToLayerChannels(ofxComp);
            if (layerChannels.size() >= 1) {
                compNames.assign(layerChannels.begin() + 1, layerChannels.end());
            }

#endif
        }
        
        _createAlpha->setIsSecret(!showCreateAlpha);
        
        if (compNames.size() == 1) {
            _r->setEnabled(false);
            _r->setIsSecret(true);
            _g->setEnabled(false);
            _g->setIsSecret(true);
            _b->setEnabled(false);
            _b->setIsSecret(true);
            _a->setEnabled(true);
            _a->setIsSecret(false);
            _a->setLabel(compNames[0]);
        } else if (compNames.size() == 2) {
            _r->setEnabled(true);
            _r->setIsSecret(false);
            _r->setLabel(compNames[0]);
            _g->setEnabled(true);
            _g->setIsSecret(false);
            _g->setLabel(compNames[1]);
            _b->setEnabled(false);
            _b->setIsSecret(true);
            _a->setEnabled(false);
            _a->setIsSecret(true);
        } else if (compNames.size() == 3) {
            _r->setEnabled(true);
            _r->setIsSecret(false);
            _r->setLabel(compNames[0]);
            _g->setEnabled(true);
            _g->setLabel(compNames[1]);
            _g->setIsSecret(false);
            _b->setEnabled(true);
            _b->setIsSecret(false);
            _b->setLabel(compNames[2]);
            _a->setEnabled(false);
            _a->setIsSecret(true);

        } else if (compNames.size() == 4) {
            _r->setEnabled(true);
            _r->setIsSecret(false);
            _r->setLabel(compNames[0]);
            _g->setEnabled(true);
            _g->setLabel(compNames[1]);
            _g->setIsSecret(false);
            _b->setEnabled(true);
            _b->setIsSecret(false);
            _b->setLabel(compNames[2]);
            _a->setEnabled(true);
            _a->setIsSecret(false);
            _a->setLabel(compNames[3]);
        } else {
            //Unsupported
            throwSuiteStatusException(kOfxStatFailed);
        }
    }
}


mDeclarePluginFactory(ShufflePluginFactory, {}, {});

void ShufflePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
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
        assert(sizeof(gOutputBitDepthMap) >= sizeof(gOutputBitDepthMap[0])*(i+1));
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
#ifdef OFX_EXTENSIONS_NATRON
            case ePixelComponentXY:
                gSupportsXY = true;
                break;
#endif
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
#ifdef OFX_EXTENSIONS_NATRON
        if (gSupportsXY) {
            gOutputComponentsMap[i] = ePixelComponentXY;
            ++i;
        }
#endif
        assert(sizeof(gOutputComponentsMap) >= sizeof(gOutputComponentsMap[0])*(i+1));
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
    
#ifdef OFX_EXTENSIONS_NATRON
    gSupportsDynamicChoices = OFX::getImageEffectHostDescription()->supportsDynamicChoices;
#else
    gSupportsDynamicChoices = false;
#endif
#ifdef OFX_EXTENSIONS_NUKE
    gIsMultiPlanar = kEnableMultiPlanar && OFX::getImageEffectHostDescription()->isMultiPlanar;
    if (gIsMultiPlanar) {
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
}

static void
addInputChannelOptionsRGBA(ChoiceParamDescriptor* outputR, InputChannelEnum def, OFX::ContextEnum context)
{
    addInputChannelOptionsRGBA(outputR, context, 0);
    outputR->setDefault(def);
}

void ShufflePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    
#ifdef OFX_EXTENSIONS_NUKE
    if (gIsMultiPlanar && !OFX::fetchSuite(kFnOfxImageEffectPlaneSuite, 2)) {
        OFX::throwHostMissingSuiteException(kFnOfxImageEffectPlaneSuite);
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
    if (!gIsMultiPlanar) {
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
#ifdef OFX_EXTENSIONS_NATRON
        if (gSupportsXY) {
            assert(gOutputComponentsMap[param->getNOptions()] == ePixelComponentXY);
            param->appendOption(kParamOutputComponentsOptionXY);
        }
#endif
        param->setDefault(0);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    } else {
        {
            ChoiceParamDescriptor *param = desc.defineChoiceParam(kNatronOfxParamOutputChannels);
            param->setLabel(kParamOutputChannelsLabel);
            param->setHint(kParamOutputChannelsHint);
#ifdef OFX_EXTENSIONS_NATRON
            param->setHostCanAddOptions(true);
#endif
            param->appendOption(kShuffleColorRGBA);
            param->appendOption(kShuffleMotionForwardPlaneName);
            param->appendOption(kShuffleMotionBackwardPlaneName);
            param->appendOption(kShuffleDisparityLeftPlaneName);
            param->appendOption(kShuffleDisparityRightPlaneName);
            if (gSupportsDynamicChoices) {
                param->setEvaluateOnChange(false);
                param->setIsPersistant(false);
            }
            desc.addClipPreferencesSlaveParam(*param); // is used as _outputComponents if multiplane
            if (page) {
                page->addChild(*param);
            }
        }
        if (gSupportsDynamicChoices) {
            //Add a hidden string param that will remember the value of the choice
            OFX::StringParamDescriptor* param = desc.defineStringParam(kParamOutputChannelsChoice);
            param->setLabel(kParamOutputChannelsLabel "Choice");
            param->setIsSecret(true);
            page->addChild(*param);
        }
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
        param->setAnimates(false);
#ifndef DEBUG
        // Shuffle only does linear conversion, which is useless for 8-bits and 16-bits formats.
        // Disable it for now (in the future, there may be colorspace conversion options)
        param->setIsSecret(true); // always secret
#endif
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }
    
    if (gSupportsRGB || gSupportsRGBA) {
        // outputR
        {
            {
                ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamOutputR);
                param->setLabel(kParamOutputRLabel);
                param->setHint(kParamOutputRHint);
                addInputChannelOptionsRGBA(param, eInputChannelAR, context);
                if (gSupportsDynamicChoices) {
                    param->setEvaluateOnChange(false);
                    param->setIsPersistant(false);
                }
                if (page) {
                    page->addChild(*param);
                }
            }
            if (gSupportsDynamicChoices) {
                //Add a hidden string param that will remember the value of the choice
                OFX::StringParamDescriptor* param = desc.defineStringParam(kParamOutputRChoice);
                param->setLabel(kParamOutputRLabel "Choice");
                param->setIsSecret(true);
                page->addChild(*param);
            }
        }
        
        // outputG
        {
            {
                ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamOutputG);
                param->setLabel(kParamOutputGLabel);
                param->setHint(kParamOutputGHint);
                if (gSupportsDynamicChoices) {
                    param->setEvaluateOnChange(false);
                    param->setIsPersistant(false);
                }
                addInputChannelOptionsRGBA(param, eInputChannelAG, context);
                if (page) {
                    page->addChild(*param);
                }
            }
            if (gSupportsDynamicChoices) {
                //Add a hidden string param that will remember the value of the choice
                OFX::StringParamDescriptor* param = desc.defineStringParam(kParamOutputGChoice);
                param->setLabel(kParamOutputGLabel "Choice");
                param->setIsSecret(true);
                page->addChild(*param);
            }
        }
        
        // outputB
        {
            {
                ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamOutputB);
                param->setLabel(kParamOutputBLabel);
                param->setHint(kParamOutputBHint);
                if (gSupportsDynamicChoices) {
                    param->setEvaluateOnChange(false);
                    param->setIsPersistant(false);
                }
                addInputChannelOptionsRGBA(param, eInputChannelAB, context);
                if (page) {
                    page->addChild(*param);
                }
            }
            if (gSupportsDynamicChoices) {
                //Add a hidden string param that will remember the value of the choice
                OFX::StringParamDescriptor* param = desc.defineStringParam(kParamOutputBChoice);
                param->setLabel(kParamOutputBLabel "Choice");
                param->setIsSecret(true);
                page->addChild(*param);
            }
        }
    }
    // ouputA
    if (gSupportsRGBA || gSupportsAlpha) {
        {
            BooleanParamDescriptor* param = desc.defineBooleanParam(kParamCreateAlpha);
            param->setLabel(kParamCreateAlphaLabel);
            param->setHint(kParamCreateAlphaHint);
            param->setDefault(false);
            if (page) {
                page->addChild(*param);
            }
            desc.addClipPreferencesSlaveParam(*param);
        }
        {
            ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamOutputA);
            param->setLabel(kParamOutputALabel);
            param->setHint(kParamOutputAHint);
            if (gSupportsDynamicChoices) {
                param->setEvaluateOnChange(false);
                param->setIsPersistant(false);
            }
            addInputChannelOptionsRGBA(param, eInputChannelAA, context);
            if (page) {
                page->addChild(*param);
            }
        }
        if (gSupportsDynamicChoices) {
            //Add a hidden string param that will remember the value of the choice
            OFX::StringParamDescriptor* param = desc.defineStringParam(kParamOutputAChoice);
            param->setLabel(kParamOutputALabel "Choice");
            param->setIsSecret(true);
            page->addChild(*param);
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
}

OFX::ImageEffect* ShufflePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new ShufflePlugin(handle, context);
}

void getShufflePluginID(OFX::PluginFactoryArray &ids)
{
    static ShufflePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

