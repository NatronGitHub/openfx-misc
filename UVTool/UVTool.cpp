/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2017 INRIA
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
 * OFX UVTool plugin.
 */

#include <cmath>
#include <cfloat> // DBL_MAX
#include <cstdlib> // atoi
#include <cstdio> // fopen
#include <iostream>
#include <sstream>
#include <set>
#include <map>
#include <vector>
#include <algorithm>
#include <limits>

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "ofxsImageEffect.h"
#include "ofxsProcessing.H"
#include "ofxsCoords.h"
#include "ofxsMaskMix.h"
#include "ofxsFilter.h"
#include "ofxsMatrix2D.h"
#include "ofxsCopier.h"
#include "ofxsMacros.h"
#include "ofxsMultiPlane.h"
#include "ofxsGenerator.h"
#include "ofxsFormatResolution.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "UVTool"
#define kPluginGrouping "Transform"
#define kPluginDescription \
    "Apply an operation on a UV map." \

#define kPluginIdentifier "net.sf.openfx.UVTool"

// History:
// version 1.0: initial version
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1 // suports tiles except when inversing the map
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamChannelU "channelU"
#define kParamChannelULabel "U Channel", "Input U channel. The output U channel is red."

#define kParamChannelUChoice kParamChannelU "Choice"

#define kParamChannelV "channelV"
#define kParamChannelVLabel "V Channel", "V channel. The output V channel is green."

#define kParamChannelVChoice kParamChannelV "Choice"

#define kParamChannelA "channelA"
#define kParamChannelALabel "Alpha Channel", "Input Alpha channel from UV. The Output alpha is set to this value. If \"Unpremult UV\" is checked, the UV values are divided by alpha."

#define kParamChannelAChoice kParamChannelA "Choice"

#define kParamChannelUnpremultUV "unpremultUV"
#define kParamChannelUnpremultUVLabel "Unpremult UV", "Unpremult input UV by Alpha from UV. Check if UV values look small for small values of Alpha (3D software sometimes write premultiplied UV values). Output UV is never premultiplied."

#define kParamPremultChanged "premultChanged"

enum InputChannelEnum
{
    eInputChannelR = 0,
    eInputChannelG,
    eInputChannelB,
    eInputChannelA,
    eInputChannel0,
    eInputChannel1,
};


#define kParamUVInputFormat "uvInputFormat"
#define kParamUVInputFormatLabel "UV Format", "How the map is computed from the U and V values."
#define kParamUVOutputFormat "uvOutputFormat"
#define kParamUVOutputFormatLabel "Output UV Format", "How the map is converted to U and V values. U and V go to the red and green channels, alpha goes to the alpha channel, U and V are never premultiplied."
#define kParamUVFormatOptionSTMap "STMap", "The U and V channels give the normalized position of the pixel where the color is taken. (0,0) is the bottom left corner of the input image, while (1,1) is the top right corner."
#define kParamUVFormatOptionIDistort "IDistort", "The U and V channels give the offset in pixels in the destination image to the pixel where the color is taken. For example, if at pixel (45,12) the UV value is (-1.5,3.2), then the color at this pixel is taken from (43.5,15.2) in the source image."
enum UVFormatEnum
{
    eUVFormatSTMap = 0,
    eUVFormatIDistort,
};

#define kParamUVOffset "uvOffset"
#define kParamUVOffsetLabel "UV Offset", "Offset to apply to the input U and V channel (useful if these were stored in a file that cannot handle negative numbers). The output U and V have standard values that correspond to the UV Output Format."

#define kParamUVScale "uvScale"
#define kParamUVScaleLabel "UV Scale", "Scale factor to apply to the input U and V channel (useful if these were stored in a file that can only store integer values). The output U and V have standard values that correspond to the UV Output Format."

#define kParamAmount "uvAmount"
#define kParamAmountLabel "Amount", "Multiply the displacement by this amount. Zero means the map corresponds to an identity transform."

static bool gIsMultiPlane;
struct InputPlaneChannel
{
    Image* img;
    int channelIndex;
    bool fillZero;

    InputPlaneChannel() : img(0), channelIndex(-1), fillZero(true) {}
};

class UVToolProcessorBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg;
    OfxRectD _format;
    std::vector<InputPlaneChannel> _planeChannels;
    bool _unpremultUV;
    UVFormatEnum _uvInputFormat;
    double _uOffset;
    double _vOffset;
    double _uScale;
    double _vScale;
    double _amount;
    UVFormatEnum _uvOutputFormat;
    OfxPointD _renderScale;

public:

    UVToolProcessorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _srcImg(0)
        , _planeChannels()
        , _unpremultUV(true)
        , _uvInputFormat(eUVFormatSTMap)
        , _uOffset(0.)
        , _vOffset(0.)
        , _uScale(1.)
        , _vScale(1.)
        , _amount(1.)
        , _uvOutputFormat(eUVFormatSTMap)
    {
        _renderScale.x = _renderScale.y = 1.;
        _format.x1 = _format.y1 = 0.;
        _format.x2 = _format.y2 = 1.;
    }

    void setSrcImgs(const Image *src) {_srcImg = src; }

    void setValues(const OfxRectD& format,
                   const std::vector<InputPlaneChannel>& planeChannels,
                   bool unpremultUV,
                   UVFormatEnum uvInputFormat,
                   double uOffset,
                   double vOffset,
                   double uScale,
                   double vScale,
                   double amount,
                   UVFormatEnum uvOutputFormat,
                   const OfxPointD& renderScale)
    {
        _format = format;
        _planeChannels = planeChannels;
        _unpremultUV = unpremultUV;
        _uvInputFormat = uvInputFormat;
        _uOffset = uOffset;
        _vOffset = vOffset;
        _uScale = uScale;
        _vScale = vScale;
        _amount = amount;
        _uvOutputFormat = uvOutputFormat;
        _renderScale = renderScale;
    }

private:
};


// The "filter" and "clamp" template parameters allow filter-specific optimization
// by the compiler, using the same generic code for all filters.
template <class PIX, int nComponents, int maxValue>
class UVToolProcessor
    : public UVToolProcessorBase
{
public:
    UVToolProcessor(ImageEffect &instance)
        : UVToolProcessorBase(instance)
    {
    }

private:


    const PIX * getPix(unsigned channel,
                       int x,
                       int y)
    {
        return (const PIX *)  (_planeChannels[channel].img ? _planeChannels[channel].img->getPixelAddress(x, y) : 0);
    }

    double getVal(unsigned channel,
                  const PIX* p,
                  const PIX* pp)
    {
        if (!_planeChannels[channel].img) {
            return _planeChannels[channel].fillZero ? 0. : 1.;
        }
        if (!p) {
            return pp ? pp[_planeChannels[channel].channelIndex] : 0.;
        }

        return p[_planeChannels[channel].channelIndex];
    }

    void unpremult(double a,
                   double *u,
                   double *v)
    {
        if ( _unpremultUV && (a != 0.) ) {
            *u /= a;
            *v /= a;
        }
    }

    void multiThreadProcessImages(OfxRectI procWindow);
};

template <class PIX, int nComponents, int maxValue>
void
UVToolProcessor<PIX, nComponents, maxValue>::multiThreadProcessImages(OfxRectI procWindow)
{
    assert(nComponents == 1 || nComponents == 3 || nComponents == 4);
    assert(_dstImg);

    // required for STMap
    double srcx1 = _format.x1;
    double srcx2 = _format.x2;
    double srcy1 = _format.y1;
    double srcy2 = _format.y2;
    float tmpPix[4];
    for (int y = procWindow.y1; y < procWindow.y2; y++) {
        if ( _effect.abort() ) {
            break;
        }

        PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

        for (int x = procWindow.x1; x < procWindow.x2; x++) {
            //double sx, sy; // the source pixel coordinates (0,0 corresponds to the lower left corner of the first pixel)
            double dx, dy; // offset to the source image in pixels at this scale
            double a = 1.;
            const PIX *uPix    = getPix(0, x, y  );
            const PIX *vPix;
            if (_planeChannels[1].img == _planeChannels[0].img) {
                vPix    = uPix;
            } else {
                vPix =    getPix(1, x, y  );
            }
            const PIX *aPix;
            if (_planeChannels[2].img == _planeChannels[0].img) {
                aPix = uPix;
            } else if (_planeChannels[2].img == _planeChannels[1].img) {
                aPix = vPix;
            } else {
                aPix =    getPix(2, x, y  );
            }
            // compute gradients before wrapping
            double u = getVal(0, uPix, NULL);
            double v = getVal(1, vPix, NULL);
            a = getVal(2, aPix, NULL);
            unpremult(a, &u, &v);

            u = (u - _uOffset) * _uScale;
            v = (v - _vOffset) * _vScale;
            if (_uvInputFormat == eUVFormatSTMap) {
                double sx, sy; // the source pixel coordinates (0,0 corresponds to the lower left corner of the first pixel)
                sx = srcx1 + u * (srcx2 - srcx1);
                sy = srcy1 + v * (srcy2 - srcy1);         // 0,0 corresponds to the lower left corner of the first pixel
                dx = sx - x - 0.5;
                dy = sy - y - 0.5;
            } else {
                assert(_uvInputFormat == eUVFormatIDistort);
                // 0,0 corresponds to the lower left corner of the first pixel, so we have to add 0.5
                // (x,y) = (0,0) and (u,v) = (0,0) means to pick color at (0.5,0.5)
                dx = u * _renderScale.x;
                dy = v * _renderScale.y;
                //sx = x + dx + 0.5;
                //sy = y + dy + 0.5;
            }

            //// PROCESS BEGIN

            // dx, dy is the source pixel position offset in pixel coords
            dx *= _amount;
            dy *= _amount;

            // TODO:
            // -fill (fill areas where alpha=0 with push-pull)
            // -validate (remove areas where the image is reversed, detJ < 0)
            
            //// PROCESS END

            if (_uvOutputFormat == eUVFormatSTMap) {
                // 0,0 corresponds to the lower left corner of the first pixel
                double sx, sy; // the source pixel coordinates (0,0 corresponds to the lower left corner of the first pixel)
                sx = x + dx + 0.5;
                sy = y + dy + 0.5;
                tmpPix[0] = (sx - srcx1) / (srcx2 - srcx1); // u
                tmpPix[1] = (sy - srcy1) / (srcy2 - srcy1); // v
            } else {
                assert(_uvOutputFormat == eUVFormatIDistort);
                // 0,0 corresponds to the lower left corner of the first pixel, so we have to add 0.5
                // (x,y) = (0,0) and (u,v) = (0,0) means to pick color at (0.5,0.5)
                tmpPix[0] = dx / _renderScale.x;
                tmpPix[1] = dy / _renderScale.y;
            }
            tmpPix[2] = 1.;
            tmpPix[3] = a;
            // tmpPix is normalized between [0,1]
            ofxsPremultPix<PIX, nComponents, maxValue>(tmpPix, //!< interpolated unpremultiplied pixel
                                                       _unpremultUV,
                                                       3,
                                                       dstPix); //!< destination pixel

            // increment the dst pixel
            dstPix += nComponents;
        }
    }
} // multiThreadProcessImages

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class UVToolPlugin
    : public MultiPlane::MultiPlaneEffect
{
public:
    /** @brief ctor */
    UVToolPlugin(OfxImageEffectHandle handle)
        : MultiPlane::MultiPlaneEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _uvChannels()
        , _unpremultUV(NULL)
        , _uvInputFormat(NULL)
        , _uvOffset(NULL)
        , _uvScale(NULL)
        , _uvOutputFormat(NULL)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentRGBA ||
                             _dstClip->getPixelComponents() == ePixelComponentAlpha) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA ||
                               _srcClip->getPixelComponents() == ePixelComponentAlpha) ) );
        _uvChannels[0] = fetchChoiceParam(kParamChannelU);
        _uvChannels[1] = fetchChoiceParam(kParamChannelV);
        _uvChannels[2] = fetchChoiceParam(kParamChannelA);
        if (gIsMultiPlane) {
            fetchDynamicMultiplaneChoiceParameter(kParamChannelU, _srcClip);
            fetchDynamicMultiplaneChoiceParameter(kParamChannelV, _srcClip);
            fetchDynamicMultiplaneChoiceParameter(kParamChannelA, _srcClip);
        }
        _unpremultUV = fetchBooleanParam(kParamChannelUnpremultUV);
        _uvInputFormat = fetchChoiceParam(kParamUVInputFormat);
        _uvOffset = fetchDouble2DParam(kParamUVOffset);
        _uvScale = fetchDouble2DParam(kParamUVScale);
        _amount = fetchDoubleParam(kParamAmount);
        _uvOutputFormat = fetchChoiceParam(kParamUVOutputFormat);
        assert(_uvChannels[0] && _uvChannels[1] && _uvChannels[2] && _uvInputFormat && _uvOffset && _uvScale && _amount && _uvOutputFormat);
    }

private:
    // override the roi call
    virtual void getRegionsOfInterest(const RegionsOfInterestArguments &args, RegionOfInterestSetter &rois) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;
#ifdef OFX_EXTENSIONS_NUKE
    virtual void getClipComponents(const ClipComponentsArguments& args, ClipComponentsSetter& clipComponents) OVERRIDE FINAL;
#endif

    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /* internal render function */
    template <int nComponents>
    void renderInternal(const RenderArguments &args, BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(UVToolProcessorBase &, const RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /** @brief called when a param has just had its value changed */
    void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    ChoiceParam* _uvChannels[3];
    BooleanParam* _unpremultUV;
    ChoiceParam* _uvInputFormat;
    Double2DParam *_uvOffset;
    Double2DParam *_uvScale;
    DoubleParam* _amount;
    ChoiceParam* _uvOutputFormat;
};

void
UVToolPlugin::getClipPreferences(ClipPreferencesSetter & /*clipPreferences*/)
{
    if (gIsMultiPlane && _srcClip) {
        buildChannelMenus();
    }
}

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */


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


////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from
static
int
getChannelIndex(InputChannelEnum e,
                PixelComponentEnum comps)
{
    int retval;

    switch (e) {
    case eInputChannelR: {
        if (
#ifdef OFX_EXTENSIONS_NATRON
            comps == ePixelComponentXY ||
#endif
            comps == ePixelComponentRGB || comps == ePixelComponentRGBA) {
            retval = 0;
        } else {
            retval = -1;
        }
        break;
    }
    case eInputChannelG: {
        if (
#ifdef OFX_EXTENSIONS_NATRON
            comps == ePixelComponentXY ||
#endif
            comps == ePixelComponentRGB || comps == ePixelComponentRGBA) {
            retval = 1;
        } else {
            retval = -1;
        }
        break;
    }
    case eInputChannelB: {
        if ( ( comps == ePixelComponentRGB) || ( comps == ePixelComponentRGBA) ) {
            retval = 2;
        } else {
            retval = -1;
        }
        break;
    }
    case eInputChannelA: {
        if (comps == ePixelComponentAlpha) {
            return 0;
        } else if (comps == ePixelComponentRGBA) {
            retval = 3;
        } else {
            retval = -1;
        }
        break;
    }
    case eInputChannel0:
    case eInputChannel1:
    default: {
        retval = -1;
        break;
    }
    } // switch

    return retval;
} // getChannelIndex

/* set up and run a processor */
void
UVToolPlugin::setupAndProcess(UVToolProcessorBase &processor,
                              const RenderArguments &args)
{
    const double time = args.time;

    std::auto_ptr<Image> dst( _dstClip->fetchImage(time) );

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
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);
    }

    InputImagesHolder_RAII imagesHolder;
    std::vector<InputPlaneChannel> planeChannels;

    if (_srcClip) {
        if (gIsMultiPlane) {
            BitDepthEnum srcBitDepth = eBitDepthNone;
            std::map<Clip*, std::map<std::string, Image*> > fetchedPlanes;
            bool isCreatingAlpha;
            for (int i = 0; i < 3; ++i) {
                InputPlaneChannel p;
                p.channelIndex = i;
                p.fillZero = false;
                //if (_srcClip) {
                Clip* clip = 0;
                std::string plane, ofxComp;
                bool ok = getPlaneNeededForParam(time, _uvChannels[i]->getName(), &clip, &plane, &ofxComp, &p.channelIndex, &isCreatingAlpha);
                if (!ok) {
                    setPersistentMessage(Message::eMessageError, "", "Cannot find requested channels in input");
                    throwSuiteStatusException(kOfxStatFailed);
                }

                p.img = 0;
                if (ofxComp == kMultiPlaneParamOutputOption0) {
                    p.fillZero = true;
                } else if (ofxComp == kMultiPlaneParamOutputOption1) {
                    p.fillZero = false;
                } else {
                    std::map<std::string, Image*>& clipPlanes = fetchedPlanes[clip];
                    std::map<std::string, Image*>::iterator foundPlane = clipPlanes.find(plane);
                    if ( foundPlane != clipPlanes.end() ) {
                        p.img = foundPlane->second;
                    } else {
#ifdef OFX_EXTENSIONS_NUKE
                        p.img = clip->fetchImagePlane( time, args.renderView, plane.c_str() );
#else
                        p.img = ( clip && clip->isConnected() ) ? clip->fetchImage(time) : 0;
#endif
                        if (p.img) {
                            clipPlanes.insert( std::make_pair(plane, p.img) );
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
                }
                //}
                planeChannels.push_back(p);
            }
        } else { //!gIsMultiPlane
            InputChannelEnum uChannel = eInputChannelR;
            InputChannelEnum vChannel = eInputChannelG;
            InputChannelEnum aChannel = eInputChannelA;
            if (_uvChannels[0]) {
                uChannel = (InputChannelEnum)_uvChannels[0]->getValueAtTime(time);
            }
            if (_uvChannels[1]) {
                vChannel = (InputChannelEnum)_uvChannels[1]->getValueAtTime(time);
            }
            if (_uvChannels[2]) {
                aChannel = (InputChannelEnum)_uvChannels[2]->getValueAtTime(time);
            }

            Image* uv = NULL;
            if ( ( ( (uChannel != eInputChannel0) && (uChannel != eInputChannel1) ) ||
                   ( (vChannel != eInputChannel0) && (vChannel != eInputChannel1) ) ||
                   ( (aChannel != eInputChannel0) && (aChannel != eInputChannel1) ) ) &&
                 ( _srcClip && _srcClip->isConnected() ) ) {
                uv =  _srcClip->fetchImage(time);
            }

            PixelComponentEnum uvComponents = ePixelComponentNone;
            if (uv) {
                imagesHolder.appendImage(uv);
                if ( (uv->getRenderScale().x != args.renderScale.x) ||
                     ( uv->getRenderScale().y != args.renderScale.y) ||
                     ( ( uv->getField() != eFieldNone) /* for DaVinci Resolve */ && ( uv->getField() != args.fieldToRender) ) ) {
                    setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                    throwSuiteStatusException(kOfxStatFailed);
                }
                BitDepthEnum uvBitDepth      = uv->getPixelDepth();
                uvComponents = uv->getPixelComponents();
                // only eBitDepthFloat is supported for now (other types require special processing for uv values)
                if ( (uvBitDepth != eBitDepthFloat) /*|| (uvComponents != dstComponents)*/ ) {
                    throwSuiteStatusException(kOfxStatErrImageFormat);
                }
            }

            // fillZero is only used when the channelIndex is -1 (i.e. it does not exist), and in this case:
            // - it is true if the inputchannel is 0, R, G or B
            // - it is false if the inputchannel is 1, A (images without alpha are considered opaque)
            {
                InputPlaneChannel u;
                u.channelIndex = getChannelIndex(uChannel, uvComponents);
                u.img = (u.channelIndex >= 0) ? uv : NULL;
                u.fillZero = (u.channelIndex >= 0) ? false : !(uChannel == eInputChannel1 || uChannel == eInputChannelA);
                planeChannels.push_back(u);
            }
            {
                InputPlaneChannel v;
                v.channelIndex = getChannelIndex(vChannel, uvComponents);
                v.img = (v.channelIndex >= 0) ? uv : NULL;
                v.fillZero = (v.channelIndex >= 0) ? false : !(vChannel == eInputChannel1 || vChannel == eInputChannelA);
                planeChannels.push_back(v);
            }
            {
                InputPlaneChannel a;
                a.channelIndex = getChannelIndex(aChannel, uvComponents);
                a.img = (a.channelIndex >= 0) ? uv : NULL;
                a.fillZero = (a.channelIndex >= 0) ? false : !(aChannel == eInputChannel1 || aChannel == eInputChannelA);
                planeChannels.push_back(a);
            }
        }
    } // if (_srcClip)"


    // set the images
    processor.setDstImg( dst.get() );
    // set the render window
    processor.setRenderWindow(args.renderWindow);

    bool unpremultUV = false;
    double uScale = 1., vScale = 1.;
    double uOffset = 0., vOffset = 0.;
    unpremultUV = _unpremultUV->getValueAtTime(time);
    _uvOffset->getValueAtTime(time, uOffset, vOffset);
    _uvScale->getValueAtTime(time, uScale, vScale);
    double amount = _amount->getValueAtTime(time);
    OfxRectD format = {0, 0, 1, 1};
    if ( _srcClip && _srcClip->isConnected() ) {
        OfxRectI formatI;
        formatI.x1 = formatI.y1 = formatI.x2 = formatI.y2 = 0; // default value
        _srcClip->getFormat(formatI);
        if ( OFX::Coords::rectIsEmpty(formatI) ) {
            // no format is available, use the RoD instead
            const OfxRectD& srcRod = _srcClip->getRegionOfDefinition(time);
            // Coords::toPixelNearest(srcRod, args.renderScale, _srcClip->getPixelAspectRatio(), &format);
            double par = _srcClip->getPixelAspectRatio();
            format.x1 = srcRod.x1 * args.renderScale.x / par;
            format.y1 = srcRod.y1 * args.renderScale.y;
            format.x2 = srcRod.x2 * args.renderScale.x / par;
            format.y2 = srcRod.y2 * args.renderScale.y;
        } else {
            format.x1 = formatI.x1 * args.renderScale.x;
            format.y1 = formatI.y1 * args.renderScale.y;
            format.x2 = formatI.x2 * args.renderScale.x;
            format.y2 = formatI.y2 * args.renderScale.y;
        }
    }
    UVFormatEnum uvInputFormat = (UVFormatEnum)_uvInputFormat->getValueAtTime(time);
    UVFormatEnum uvOutputFormat = (UVFormatEnum)_uvOutputFormat->getValueAtTime(time);

    processor.setValues(format,
                        planeChannels,
                        unpremultUV,
                        uvInputFormat,
                        uOffset, vOffset,
                        uScale, vScale,
                        amount,
                        uvOutputFormat,
                        args.renderScale);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // DistortionPlugin::setupAndProcess

// the internal render function
template <int nComponents>
void
UVToolPlugin::renderInternal(const RenderArguments &args,
                             BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case eBitDepthUByte: {
        UVToolProcessor<unsigned char, nComponents, 255> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthUShort: {
        UVToolProcessor<unsigned short, nComponents, 65535> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthFloat:  {
        UVToolProcessor<float, nComponents, 1> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
UVToolPlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
#ifdef OFX_EXTENSIONS_NATRON
    assert(dstComponents == ePixelComponentAlpha || dstComponents == ePixelComponentXY || dstComponents == ePixelComponentRGB || dstComponents == ePixelComponentRGBA);
#else
    assert(dstComponents == ePixelComponentAlpha || dstComponents == ePixelComponentRGB || dstComponents == ePixelComponentRGBA);
#endif
    if (dstComponents == ePixelComponentRGBA) {
        renderInternal<4>(args, dstBitDepth);
    } else if (dstComponents == ePixelComponentRGB) {
        renderInternal<3>(args, dstBitDepth);
#ifdef OFX_EXTENSIONS_NATRON
    } else if (dstComponents == ePixelComponentXY) {
        renderInternal<2>(args, dstBitDepth);
#endif
    } else {
        assert(dstComponents == ePixelComponentAlpha);
        renderInternal<1>(args, dstBitDepth);
    }
} // UVToolPlugin::render

bool
UVToolPlugin::isIdentity(const IsIdentityArguments &args,
                         Clip * &identityClip,
                         double & /*identityTime*/)
{
    const double time = args.time;

    return false;
} // DistortionPlugin::isIdentity

// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
void
UVToolPlugin::getRegionsOfInterest(const RegionsOfInterestArguments &args,
                                   RegionOfInterestSetter &rois)
{
    const double time = args.time;

    if ( !_srcClip || !_srcClip->isConnected() ) {
        return;
    }

#warning "TODO: if (inverse)"
    /*
       if (inverse) {
       // ask for full RoD of srcClip
       const OfxRectD& srcRod = _srcClip->getRegionOfDefinition(time);
       rois.setRegionOfInterest(*_srcClip, srcRod);
       } else { */
    // only ask for the renderWindow (intersected with the RoD) from uvClip
    if (_srcClip) {
        OfxRectD uvRoI = _srcClip->getRegionOfDefinition(time);
        Coords::rectIntersection(uvRoI, args.regionOfInterest, &uvRoI);
        rois.setRegionOfInterest(*_srcClip, uvRoI);
    }
}

bool
UVToolPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                    OfxRectD &rod)
{
    const double time = args.time;

    return false;
}

#ifdef OFX_EXTENSIONS_NUKE
void
UVToolPlugin::getClipComponents(const ClipComponentsArguments& args,
                                ClipComponentsSetter& clipComponents)
{
    assert(gIsMultiPlane);

    const double time = args.time;
    PixelComponentEnum dstPx = _dstClip->getPixelComponents();
    clipComponents.addClipComponents(*_dstClip, dstPx);

    if (_srcClip) {
        std::map<Clip*, std::set<std::string> > clipMap;
        bool isCreatingAlpha;
        for (int i = 0; i < 2; ++i) {
            std::string ofxComp, ofxPlane;
            int channelIndex;
            Clip* clip = 0;
            bool ok = getPlaneNeededForParam(time, _uvChannels[i]->getName(), &clip, &ofxPlane, &ofxComp, &channelIndex, &isCreatingAlpha);
            if (!ok) {
                continue;
            }
            if ( (ofxComp == kMultiPlaneParamOutputOption0) || (ofxComp == kMultiPlaneParamOutputOption1) ) {
                continue;
            }
            assert(clip);

            std::map<Clip*, std::set<std::string> >::iterator foundClip = clipMap.find(clip);
            if ( foundClip == clipMap.end() ) {
                std::set<std::string> s;
                s.insert(ofxComp);
                clipMap.insert( std::make_pair(clip, s) );
                clipComponents.addClipComponents(*clip, ofxComp);
            } else {
                std::pair<std::set<std::string>::iterator, bool> ret = foundClip->second.insert(ofxComp);
                if (ret.second) {
                    clipComponents.addClipComponents(*clip, ofxComp);
                }
            }
        }
    }
} // getClipComponents

#endif

void
UVToolPlugin::changedParam(const InstanceChangedArgs &args,
                           const std::string &paramName)
{
    if (gIsMultiPlane) {
        if ( handleChangedParamForAllDynamicChoices(paramName, args.reason) ) {
            return;
        }
    }
}

mDeclarePluginFactory(UVToolPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
UVToolPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    //desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    //desc.addSupportedContext(eContextPaint);
    //desc.addSupportedBitDepth(eBitDepthUByte); // not yet supported (requires special processing for uv clip values)
    //desc.addSupportedBitDepth(eBitDepthUShort);
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
    desc.setSupportsRenderQuality(true);
#ifdef OFX_EXTENSIONS_NUKE
    // ask the host to render all planes
    desc.setPassThroughForNotProcessedPlanes(ePassThroughLevelRenderAllRequestedPlanes);
#endif

#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone); // we have our own channel selector
#endif


    gIsMultiPlane = false;

#if defined(OFX_EXTENSIONS_NUKE) && defined(OFX_EXTENSIONS_NATRON)
    gIsMultiPlane = getImageEffectHostDescription()->supportsDynamicChoices && getImageEffectHostDescription()->isMultiPlanar;
    if (gIsMultiPlane) {
        // This enables fetching different planes from the input.
        // Generally the user will read a multi-layered EXR file in the Reader node and then use the shuffle
        // to redirect the plane's channels into RGBA color plane.

        desc.setIsMultiPlanar(true);
    }
#endif
} // >::describe

void
UVToolPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                       ContextEnum /*context*/)
{
#ifdef OFX_EXTENSIONS_NUKE
    if ( gIsMultiPlane && !fetchSuite(kFnOfxImageEffectPlaneSuite, 2, true) ) {
        throwHostMissingSuiteException(kFnOfxImageEffectPlaneSuite);
    }
#endif

    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NUKE
    srcClip->addSupportedComponent(ePixelComponentXY);
#endif
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
#ifdef OFX_EXTENSIONS_NUKE
    srcClip->setCanTransform(true); // we can concatenate transforms upwards on srcClip only
#endif
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    //dstClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NUKE
    //dstClip->addSupportedComponent(ePixelComponentXY);
#endif
    //dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    std::vector<std::string> clipsForChannels(1);
    clipsForChannels[0] = kOfxImageEffectSimpleSourceClipName;

    if (gIsMultiPlane) {
        {
            ChoiceParamDescriptor* param = MultiPlane::Factory::describeInContextAddChannelChoice(desc, page, clipsForChannels, kParamChannelU, kParamChannelULabel);
#ifdef OFX_EXTENSIONS_NUKE
            param->setLayoutHint(eLayoutHintNoNewLine, 1);
#endif
            param->setDefault(eInputChannelR);
        }
        {
            ChoiceParamDescriptor* param = MultiPlane::Factory::describeInContextAddChannelChoice(desc, page, clipsForChannels, kParamChannelV, kParamChannelVLabel);
            param->setDefault(eInputChannelG);
        }
        {
            ChoiceParamDescriptor* param = MultiPlane::Factory::describeInContextAddChannelChoice(desc, page, clipsForChannels, kParamChannelA, kParamChannelALabel);
            param->setDefault(eInputChannelA);
        }
    } else {
        {
            ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamChannelU);
            param->setLabelAndHint(kParamChannelULabel);
#ifdef OFX_EXTENSIONS_NUKE
            param->setLayoutHint(eLayoutHintNoNewLine, 1);
#endif
            MultiPlane::Factory::addInputChannelOptionsRGBA(param, clipsForChannels, true);
            param->setDefault(eInputChannelR);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamChannelV);
            param->setLabelAndHint(kParamChannelVLabel);
            MultiPlane::Factory::addInputChannelOptionsRGBA(param, clipsForChannels, true);
            param->setDefault(eInputChannelG);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamChannelA);
            param->setLabelAndHint(kParamChannelALabel);
            MultiPlane::Factory::addInputChannelOptionsRGBA(param, clipsForChannels, true);
            param->setDefault(eInputChannelA);
            if (page) {
                page->addChild(*param);
            }
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamChannelUnpremultUV);
        param->setLabelAndHint(kParamChannelUnpremultUVLabel);
        param->setDefault(false);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamUVInputFormat);
        param->setLabelAndHint(kParamUVInputFormatLabel);
        assert(param->getNOptions() == eUVFormatSTMap);
        param->appendOption(kParamUVFormatOptionSTMap);
        assert(param->getNOptions() == eUVFormatIDistort);
        param->appendOption(kParamUVFormatOptionIDistort);
        param->setDefault(eUVFormatSTMap);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamUVOffset);
        param->setLabelAndHint(kParamUVOffsetLabel);
        param->setDefault(0., 0.);
        param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0., 0., 1., 1.);
        param->setDimensionLabels("U", "V");
        param->setUseHostNativeOverlayHandle(false);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamUVScale);
        param->setLabelAndHint(kParamUVScaleLabel);
        param->setDoubleType(eDoubleTypeScale);
        param->setDefault(1., 1.);
        param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0., 0., 100., 100.);
        param->setDimensionLabels("U", "V");
        param->setUseHostNativeOverlayHandle(false);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamAmount);
        param->setLabelAndHint(kParamAmountLabel);
        param->setDoubleType(eDoubleTypeScale);
        param->setDefault(1.);
        param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0., 2.);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamUVOutputFormat);
        param->setLabelAndHint(kParamUVOutputFormatLabel);
        assert(param->getNOptions() == eUVFormatSTMap);
        param->appendOption(kParamUVFormatOptionSTMap);
        assert(param->getNOptions() == eUVFormatIDistort);
        param->appendOption(kParamUVFormatOptionIDistort);
        param->setDefault(eUVFormatSTMap);
        if (page) {
            page->addChild(*param);
        }
    }
} // >::describeInContext

ImageEffect*
UVToolPluginFactory::createInstance(OfxImageEffectHandle handle,
                                    ContextEnum /*context*/)
{
    return new UVToolPlugin(handle);
}

static UVToolPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
