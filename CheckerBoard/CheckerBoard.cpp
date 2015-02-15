/*
 OFX CheckerBoard plugin.

 Copyright (C) 2015 INRIA

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

#include "CheckerBoard.h"

#include <cmath>
#ifdef _WINDOWS
#include <windows.h>
#endif
#include <climits>

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxsGenerator.h"
#include "ofxsLut.h"

#define kPluginName "CheckerBoardOFX"
#define kPluginGrouping "Image"
#define kPluginDescription "Generate an image with a checkerboard. A frame range may be specified for operators that need it."
#define kPluginIdentifier "net.sf.openfx.CheckerBoardPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe

#define kParamBoxSize "boxSize"
#define kParamBoxSizeLabel "Box Size"
#define kParamBoxSizeHint "Size of the checkerboard boxes in pixels."

#define kParamColor0 "color0"
#define kParamColor0Label "Color 0"
#define kParamColor0Hint "Color to fill the box on top-left of image center and every other row and column."

#define kParamColor1 "color1"
#define kParamColor1Label "Color 1"
#define kParamColor1Hint "Color to fill the box on top-right of image center and every other row and column."

#define kParamColor2 "color2"
#define kParamColor2Label "Color 2"
#define kParamColor2Hint "Color to fill the box on bottom-right of image center and every other row and column."

#define kParamColor3 "color3"
#define kParamColor3Label "Color 3"
#define kParamColor3Hint "Color to fill the box on bottom-left of image center and every other row and column."

#define kParamLineColor "lineColor"
#define kParamLineColorLabel "Line Color"
#define kParamLineColorHint "Color of the line drawn between boxes."

#define kParamLineWidth "lineWidth"
#define kParamLineWidthLabel "Line Width"
#define kParamLineWidthHint "Width, in pixels, of the lines drawn between boxes."

#define kParamCenterLineColor "centerlineColor"
#define kParamCenterLineColorLabel "Centerline Color"
#define kParamCenterLineColorHint "Color of the center lines."

#define kParamCenterLineWidth "centerlineWidth"
#define kParamCenterLineWidthLabel "Centerline Width"
#define kParamCenterLineWidthHint "Width, in pixels, of the center lines."

#define kParamRange "frameRange"
#define kParamRangeLabel "Frame Range"
#define kParamRangeHint "Time domain."

#include "ofxNatron.h"

static bool gHostIsNatron   = false;

/** @brief  Base class used to blend two images together */
class CheckerBoardProcessorBase : public OFX::ImageProcessor {
protected:
    OfxPointD _boxSize;
    OfxRGBAColourD _color0;
    OfxRGBAColourD _color1;
    OfxRGBAColourD _color2;
    OfxRGBAColourD _color3;
    OfxRGBAColourD _lineColor;
    double _lineInf, _lineSup;
    OfxRGBAColourD _centerlineColor;
    double _centerlineInf, _centerlineSup;
    OfxRectD _rod;

public:
    /** @brief no arg ctor */
    CheckerBoardProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _lineInf(0.)
    , _lineSup(0.)
    , _centerlineInf(0.)
    , _centerlineSup(0.)
    {
        _boxSize.x = _boxSize.y = 0.;
        _color0.r = _color0.g = _color0.b = _color0.a = 0.;
        _color1.r = _color1.g = _color1.b = _color1.a = 0.;
        _color2.r = _color2.g = _color2.b = _color2.a = 0.;
        _color3.r = _color3.g = _color3.b = _color3.a = 0.;
        _lineColor.r = _lineColor.g = _lineColor.b = _lineColor.a = 0.;
        _centerlineColor.r = _centerlineColor.g = _centerlineColor.b = _centerlineColor.a = 0.;
        _rod.x1 = _rod.x2 = _rod.y1 = _rod.y2 = 0.;
    }

    /** @brief set the color */
    void setValues(const OfxPointD &renderScale,
                   const OfxPointD &boxSize,
                   const OfxRGBAColourD &color0,
                   const OfxRGBAColourD &color1,
                   const OfxRGBAColourD &color2,
                   const OfxRGBAColourD &color3,
                   const OfxRGBAColourD &lineColor,
                   double lineWidth,
                   const OfxRGBAColourD &centerlineColor,
                   double centerlineWidth,
                   const OfxRectD &rod)
    {
        _boxSize.x = std::max(1., boxSize.x * renderScale.x);
        _boxSize.y = std::max(1., boxSize.y * renderScale.y);
        _color0 = color0;
        _color1 = color1;
        _color2 = color2;
        _color3 = color3;
        _lineColor = lineColor;
        _lineInf = std::max(0., lineWidth * renderScale.x / 2) + 0.25;
        _lineSup = lineWidth > 0. ? (std::max(lineWidth,1.) * renderScale.x / 2 - 0.25) : 0.;
        _centerlineColor = centerlineColor;
        _centerlineInf = std::max(0., centerlineWidth * renderScale.x / 2) + 0.25;
        _centerlineSup = centerlineWidth > 0. ? (std::max(centerlineWidth,1.) * renderScale.x / 2 - 0.25) : 0.;
        _rod.x1 = rod.x1 * renderScale.x;
        _rod.x2 = rod.x2 * renderScale.x;
        _rod.y1 = rod.y1 * renderScale.y;
        _rod.y2 = rod.y2 * renderScale.y;
    }
};

/** @brief templated class to blend between two images */
template <class PIX, int nComponents, int max>
class CheckerBoardProcessor : public CheckerBoardProcessorBase
{
public:
    // ctor
    CheckerBoardProcessor(OFX::ImageEffect &instance)
    : CheckerBoardProcessorBase(instance)
    {
    }

private:
    static void
    colorToPIX(const OfxRGBAColourD& color, PIX colorPix[nComponents])
    {
        float colorf[4];
        if (nComponents == 1) {
            // alpha
            colorf[0] = (float)color.a;
        } else if (nComponents == 3) {
            // rgb
            colorf[0] = (float)color.r;
            colorf[1] = (float)color.g;
            colorf[2] = (float)color.b;
        } else {
            assert(nComponents == 4);
            // rgba
            colorf[0] = (float)color.r;
            colorf[1] = (float)color.g;
            colorf[2] = (float)color.b;
            colorf[3] = (float)color.a;
        }


        if (max == 1) { // implies float, don't clamp
            for (int c = 0; c < nComponents; ++c) {
                colorPix[c] = colorf[c];
            }
        } else {
            // color is supposed to be linear: delinearize first
            if (nComponents == 3 || nComponents == 4) {
                // don't delinearize alpha: it is always linear
                for (int c = 0; c < 3; ++c) {
                    if (max == 255) {
                        colorf[c] = OFX::Color::to_func_srgb(colorf[c]);
                    } else {
                        assert(max == 65535);
                        colorf[c] = OFX::Color::to_func_Rec709(colorf[c]);
                    }
                }
            }
            // clamp and convert to the destination type
            for (int c = 0; c < nComponents; ++c) {
                colorPix[c] = OFX::Color::floatToInt<max+1>(colorf[c]);
            }
        }
    }

    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        PIX color0[nComponents];
        PIX color1[nComponents];
        PIX color2[nComponents];
        PIX color3[nComponents];
        PIX lineColor[nComponents];
        PIX centerlineColor[nComponents];
        colorToPIX(_color0, color0);
        colorToPIX(_color1, color1);
        colorToPIX(_color2, color2);
        colorToPIX(_color3, color3);
        colorToPIX(_lineColor, lineColor);
        colorToPIX(_centerlineColor, centerlineColor);
        OfxPointD center;
        center.x = (_rod.x1 + _rod.x2) / 2;
        center.y = (_rod.y1 + _rod.y2) / 2;

        // push pixels
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if (_effect.abort()) {
                break;
            }
            
            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            // check if we are on the centerline
            if ((center.y - _centerlineInf) <= y && y < (center.y + _centerlineSup)) {
                for (int x = procWindow.x1; x < procWindow.x2; x++) {
                    for (int c = 0; c < nComponents; ++c) {
                        dstPix[c] = centerlineColor[c];
                    }
                    dstPix += nComponents;
                }
            } else {
                // the closest line between boxes
                double yline = center.y + _boxSize.y * std::floor((y - center.y) / _boxSize.y + 0.5);
                // check if we are on a line
                if ((yline - _lineInf) <= y && y < (yline + _lineSup)) {
                    for (int x = procWindow.x1; x < procWindow.x2; x++) {
                        for (int c = 0; c < nComponents; ++c) {
                            dstPix[c] = lineColor[c];
                        }
                        dstPix += nComponents;
                    }
                } else {
                    // draw boxes and vertical lines
                    int ybox = std::floor((y - center.y) / _boxSize.y);
                    PIX *c0 = (ybox & 1) ? color3 : color0;
                    PIX *c1 = (ybox & 1) ? color2 : color1;

                    for (int x = procWindow.x1; x < procWindow.x2; x++) {
                        // check if we are on the centerline
                        if ((center.x - _centerlineInf) <= x && x < (center.x + _centerlineSup)) {
                            for (int c = 0; c < nComponents; ++c) {
                                dstPix[c] = centerlineColor[c];
                            }
                        } else {
                            // the closest line between boxes
                            double xline = center.x + _boxSize.x * std::floor((x - center.x) / _boxSize.x + 0.5);
                            // check if we are on a line
                            if ((xline - _lineInf) <= x && x < (xline + _lineSup)) {
                                for (int c = 0; c < nComponents; ++c) {
                                    dstPix[c] = lineColor[c];
                                }
                            } else {
                                // draw box
                                int xbox = std::floor((x - center.x) / _boxSize.x);
                                if (xbox & 1) {
                                    for (int c = 0; c < nComponents; ++c) {
                                        dstPix[c] = c1[c];
                                    }
                                } else {
                                    for (int c = 0; c < nComponents; ++c) {
                                        dstPix[c] = c0[c];
                                    }
                                }
                            }
                        }
                        dstPix += nComponents;
                    } // for(y)
                }
            }
        } // for(y)
    }

};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class CheckerBoardPlugin : public GeneratorPlugin
{
public:
    /** @brief ctor */
    CheckerBoardPlugin(OfxImageEffectHandle handle)
    : GeneratorPlugin(handle)
    , _boxSize(0)
    , _color0(0)
    , _color1(0)
    , _color2(0)
    , _color3(0)
    , _lineColor(0)
    , _lineWidth(0)
    , _centerlineColor(0)
    , _centerlineWidth(0)
    , _range(0)
    {
        _boxSize = fetchDouble2DParam(kParamBoxSize);
        _color0 = fetchRGBAParam(kParamColor0);
        _color1 = fetchRGBAParam(kParamColor1);
        _color2 = fetchRGBAParam(kParamColor2);
        _color3 = fetchRGBAParam(kParamColor3);
        _lineColor = fetchRGBAParam(kParamLineColor);
        _lineWidth = fetchDoubleParam(kParamLineWidth);
        _centerlineColor = fetchRGBAParam(kParamCenterLineColor);
        _centerlineWidth = fetchDoubleParam(kParamCenterLineWidth);
        _range   = fetchInt2DParam(kParamRange);
        assert(_size && _color0 && _color1 && _color2 && _color3 && _lineColor && _lineWidth && _centerlineColor && _centerlineWidth && _range);
    }

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /* override the time domain action, only for the general context */
    virtual bool getTimeDomain(OfxRangeD &range) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(CheckerBoardProcessorBase &, const OFX::RenderArguments &args);
    
    virtual bool isIdentity(const OFX::IsIdentityArguments &args,
                               OFX::Clip * &identityClip,
                               double &identityTime) OVERRIDE FINAL;

private:
    OFX::Double2DParam *_boxSize;
    OFX::RGBAParam  *_color0;
    OFX::RGBAParam  *_color1;
    OFX::RGBAParam  *_color2;
    OFX::RGBAParam  *_color3;
    OFX::RGBAParam  *_lineColor;
    OFX::DoubleParam *_lineWidth;
    OFX::RGBAParam  *_centerlineColor;
    OFX::DoubleParam *_centerlineWidth;
    OFX::Int2DParam  *_range;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


/* set up and run a processor */
void
CheckerBoardPlugin::setupAndProcess(CheckerBoardProcessorBase &processor, const OFX::RenderArguments &args)
{
    const double time = args.time;
    // get a dst image
    std::auto_ptr<OFX::Image>  dst(_dstClip->fetchImage(time));
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

    // set the images
    processor.setDstImg(dst.get());

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    OfxPointD boxSize;
    _boxSize->getValueAtTime(time, boxSize.x, boxSize.y);
    OfxRGBAColourD color0;
    _color0->getValueAtTime(time, color0.r, color0.g, color0.b, color0.a);
    OfxRGBAColourD color1;
    _color1->getValueAtTime(time, color1.r, color1.g, color1.b, color1.a);
    OfxRGBAColourD color2;
    _color2->getValueAtTime(time, color2.r, color2.g, color2.b, color2.a);
    OfxRGBAColourD color3;
    _color3->getValueAtTime(time, color3.r, color3.g, color3.b, color3.a);
    OfxRGBAColourD lineColor;
    _lineColor->getValueAtTime(time, lineColor.r, lineColor.g, lineColor.b, lineColor.a);
    double lineWidth;
    _lineWidth->getValueAtTime(time, lineWidth);
    OfxRGBAColourD centerlineColor;
    _centerlineColor->getValueAtTime(time, centerlineColor.r, centerlineColor.g, centerlineColor.b, centerlineColor.a);
    double centerlineWidth;
    _centerlineWidth->getValueAtTime(time, centerlineWidth);
    OfxRectD rod = {0, 0, 0, 0};
    if (!getRegionOfDefinition(rod)) {
        OfxPointD siz = getProjectSize();
        OfxPointD off = getProjectOffset();
        rod.x1 = off.x;
        rod.x2 = off.x + siz.x;
        rod.y1 = off.y;
        rod.y2 = off.y + siz.y;
    }
    processor.setValues(args.renderScale, boxSize, color0, color1, color2, color3, lineColor, lineWidth, centerlineColor, centerlineWidth, rod);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

// the overridden render function
void
CheckerBoardPlugin::render(const OFX::RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();
    assert(dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA || dstComponents == OFX::ePixelComponentAlpha);

    checkComponents(dstBitDepth, dstComponents);

    // do the rendering
    if (dstComponents == OFX::ePixelComponentRGBA) {
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte: {
                CheckerBoardProcessor<unsigned char, 4, 255> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort: {
                CheckerBoardProcessor<unsigned short, 4, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat: {
                CheckerBoardProcessor<float, 4, 1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte: {
                CheckerBoardProcessor<unsigned char, 3, 255> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort: {
                CheckerBoardProcessor<unsigned short, 3, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat: {
                CheckerBoardProcessor<float, 3, 1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        switch (dstBitDepth)
        {
            case OFX::eBitDepthUByte: {
                CheckerBoardProcessor<unsigned char, 1, 255> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort: {
                CheckerBoardProcessor<unsigned short, 1, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat: {
                CheckerBoardProcessor<float, 1, 1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}

/* override the time domain action, only for the general context */
bool
CheckerBoardPlugin::getTimeDomain(OfxRangeD &range)
{
    // this should only be called in the general context, ever!
    if (getContext() == OFX::eContextGeneral) {
        // how many frames on the input clip
        //OfxRangeD srcRange = _srcClip->getFrameRange();

        int min, max;
        _range->getValue(min, max);
        range.min = min;
        range.max = max;
        return true;
    }

    return false;
}

bool
CheckerBoardPlugin::isIdentity(const OFX::IsIdentityArguments &args,
                                OFX::Clip * &identityClip,
                                double &identityTime)
{
    
    if (gHostIsNatron) {
        
        // only Natron supports setting the identityClip to the output clip
        
        int min, max;
        _range->getValue(min, max);
        
        int type_i;
        _type->getValue(type_i);
        GeneratorTypeEnum type = (GeneratorTypeEnum)type_i;
        bool paramsNotAnimated = (_boxSize->getNumKeys() == 0 &&
                                  _color0->getNumKeys() == 0 &&
                                  _color1->getNumKeys() == 0 &&
                                  _color2->getNumKeys() == 0 &&
                                  _color3->getNumKeys() == 0 &&
                                  _lineColor->getNumKeys() == 0 &&
                                  _lineWidth->getNumKeys() == 0 &&
                                  _centerlineColor->getNumKeys() == 0 &&
                                  _centerlineWidth->getNumKeys() == 0);
        if (type == eGeneratorTypeSize) {
            ///If not animated and different than 'min' time, return identity on the min time.
            ///We need to check more parameters
            if (paramsNotAnimated && _size->getNumKeys() == 0 && _btmLeft->getNumKeys() == 0 && args.time != min) {
                identityClip = _dstClip;
                identityTime = min;
                return true;
            }
        } else {
            ///If not animated and different than 'min' time, return identity on the min time.
            if (paramsNotAnimated && args.time != min) {
                identityClip = _dstClip;
                identityTime = min;
                return true;
            }
        }
        
        
       
    }
    return false;
}

using namespace OFX;

mDeclarePluginFactory(CheckerBoardPluginFactory, {}, {});

void CheckerBoardPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextGenerator);
    desc.addSupportedContext(eContextGeneral);
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
    desc.setRenderTwiceAlways(false);
    desc.setRenderThreadSafety(kRenderThreadSafety);
    
    generatorDescribeInteract(desc);
}

void CheckerBoardPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{
    gHostIsNatron = (OFX::getImageEffectHostDescription()->hostName == kOfxNatronHostName);
    
    // there has to be an input clip, even for generators
    ClipDescriptor* srcClip = desc.defineClip( kOfxImageEffectSimpleSourceClipName );
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setOptional(true);

    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);
    
    PageParamDescriptor *page = desc.definePageParam("Controls");
    
    generatorDescribeInContext(page, desc, *dstClip, context);

#define kParamSize "boxsize"
#define kParamSizeLabel "Size"
#define kParamSizerHint "Size of the checkerboard boxes in pixels."

    // boxSize
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamBoxSize);
        param->setLabels(kParamBoxSizeLabel, kParamBoxSizeLabel, kParamBoxSizeLabel);
        param->setHint(kParamBoxSizeHint);
        param->setDefault(64, 64);
        param->setRange(1, 1, INT_MAX, INT_MAX);
        param->setDisplayRange(0, 0, 100, 100);
        //param->setDefault(64, 64);
        param->setAnimates(true); // can animate
        page->addChild(*param);
    }

    // color0
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamColor0);
        param->setLabels(kParamColor0Label, kParamColor0Label, kParamColor0Label);
        param->setHint(kParamColor0Hint);
        param->setDefault(0.1, 0.1, 0.1, 1.0);
        param->setRange(INT_MIN, INT_MIN, INT_MIN, INT_MIN, INT_MAX, INT_MAX, INT_MAX, INT_MAX);
        param->setDisplayRange(0, 0, 0, 0, 1, 1, 1, 1);
        param->setAnimates(true); // can animate
        page->addChild(*param);
    }

    // color1
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamColor1);
        param->setLabels(kParamColor1Label, kParamColor1Label, kParamColor1Label);
        param->setHint(kParamColor1Hint);
        param->setDefault(0.5, 0.5, 0.5, 1.0);
        param->setRange(INT_MIN, INT_MIN, INT_MIN, INT_MIN, INT_MAX, INT_MAX, INT_MAX, INT_MAX);
        param->setDisplayRange(0, 0, 0, 0, 1, 1, 1, 1);
        param->setAnimates(true); // can animate
        page->addChild(*param);
    }

    // color2
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamColor2);
        param->setLabels(kParamColor2Label, kParamColor2Label, kParamColor2Label);
        param->setHint(kParamColor2Hint);
        param->setDefault(0.1, 0.1, 0.1, 1.0);
        param->setRange(INT_MIN, INT_MIN, INT_MIN, INT_MIN, INT_MAX, INT_MAX, INT_MAX, INT_MAX);
        param->setDisplayRange(0, 0, 0, 0, 1, 1, 1, 1);
        param->setAnimates(true); // can animate
        page->addChild(*param);
    }

    // color3
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamColor3);
        param->setLabels(kParamColor3Label, kParamColor3Label, kParamColor3Label);
        param->setHint(kParamColor3Hint);
        param->setDefault(0.5, 0.5, 0.5, 1.0);
        param->setRange(INT_MIN, INT_MIN, INT_MIN, INT_MIN, INT_MAX, INT_MAX, INT_MAX, INT_MAX);
        param->setDisplayRange(0, 0, 0, 0, 1, 1, 1, 1);
        param->setAnimates(true); // can animate
        page->addChild(*param);
    }


    // linecolor
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamLineColor);
        param->setLabels(kParamLineColorLabel, kParamLineColorLabel, kParamLineColorLabel);
        param->setHint(kParamLineColorHint);
        param->setDefault(1.0, 1.0, 1.0, 1.0);
        param->setRange(INT_MIN, INT_MIN, INT_MIN, INT_MIN, INT_MAX, INT_MAX, INT_MAX, INT_MAX);
        param->setDisplayRange(0, 0, 0, 0, 1, 1, 1, 1);
        param->setAnimates(true); // can animate
        page->addChild(*param);
    }

    // lineWidth
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamLineWidth);
        param->setLabels(kParamLineWidthLabel, kParamLineWidthLabel, kParamLineWidthLabel);
        param->setHint(kParamLineWidthHint);
        param->setDefault(0.);
        param->setRange(0., INT_MAX);
        param->setDisplayRange(0, 10);
        param->setAnimates(true); // can animate
        page->addChild(*param);
    }


    // centerlineColor
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamCenterLineColor);
        param->setLabels(kParamCenterLineColorLabel, kParamCenterLineColorLabel, kParamCenterLineColorLabel);
        param->setHint(kParamCenterLineColorHint);
        param->setDefault(1.0, 1.0, 0.0, 1.0);
        param->setRange(INT_MIN, INT_MIN, INT_MIN, INT_MIN, INT_MAX, INT_MAX, INT_MAX, INT_MAX);
        param->setDisplayRange(0, 0, 0, 0, 1, 1, 1, 1);
        param->setAnimates(true); // can animate
        page->addChild(*param);
    }

    // centerlineWidth
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamCenterLineWidth);
        param->setLabels(kParamCenterLineWidthLabel, kParamCenterLineWidthLabel, kParamCenterLineWidthLabel);
        param->setHint(kParamCenterLineWidthHint);
        param->setDefault(1);
        param->setRange(0., INT_MAX);
        param->setDisplayRange(0, 10);
        param->setAnimates(true); // can animate
        page->addChild(*param);
    }

    // range
    {
        Int2DParamDescriptor *param = desc.defineInt2DParam(kParamRange);
        param->setLabels(kParamRangeLabel, kParamRangeLabel, kParamRangeLabel);
        param->setHint(kParamRangeHint);
        param->setDefault(1, 1);
        param->setDimensionLabels("min", "max");
        param->setAnimates(false); // can not animate, because it defines the time domain
        page->addChild(*param);
    }
}

ImageEffect* CheckerBoardPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    return new CheckerBoardPlugin(handle);
}

void getCheckerBoardPluginID(OFX::PluginFactoryArray &ids)
{
    static CheckerBoardPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
