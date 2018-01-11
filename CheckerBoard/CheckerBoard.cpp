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
 * OFX CheckerBoard plugin.
 */

#include <cmath>
#include <algorithm>
#include <climits>
#include <cfloat> // DBL_MAX

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxsGenerator.h"
#include "ofxsLut.h"
#ifdef OFX_EXTENSIONS_NATRON
#include "ofxNatron.h"
#endif
#include "ofxsThreadSuite.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "CheckerBoardOFX"
#define kPluginGrouping "Image"
#define kPluginDescription \
    "Generate an image with a checkerboard.\n" \
    "A frame range may be specified for operators that need it.\n" \
    "See also: http://opticalenquiry.com/nuke/index.php?title=Constant,_CheckerBoard,_ColorBars,_ColorWheel"

#define kPluginIdentifier "net.sf.openfx.CheckerBoardPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsByte true
#define kSupportsUShort true
#define kSupportsHalf false
#define kSupportsFloat true

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
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

/** @brief  Base class used to blend two images together */
class CheckerBoardProcessorBase
    : public ImageProcessor
{
protected:
    OfxPointD _boxSize;
    OfxRGBAColourD _color0;
    OfxRGBAColourD _color1;
    OfxRGBAColourD _color2;
    OfxRGBAColourD _color3;
    OfxRGBAColourD _lineColor;
    double _lineInfX, _lineSupX;
    double _lineInfY, _lineSupY;
    OfxRGBAColourD _centerlineColor;
    double _centerlineInfX, _centerlineSupX;
    double _centerlineInfY, _centerlineSupY;
    OfxRectD _rod;

public:
    /** @brief no arg ctor */
    CheckerBoardProcessorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _lineInfX(0.)
        , _lineSupX(0.)
        , _lineInfY(0.)
        , _lineSupY(0.)
        , _centerlineInfX(0.)
        , _centerlineSupX(0.)
        , _centerlineInfY(0.)
        , _centerlineSupY(0.)
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
                   double pixelAspectRatio,
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
        if (pixelAspectRatio == 0) {
            pixelAspectRatio = 1.;
        }
        _boxSize.x = std::max(1., boxSize.x * renderScale.x / pixelAspectRatio);
        _boxSize.y = std::max(1., boxSize.y * renderScale.y);
        _color0 = color0;
        _color1 = color1;
        _color2 = color2;
        _color3 = color3;
        _lineColor = lineColor;
        _lineInfX = std::max(0., lineWidth * renderScale.x / 2 / pixelAspectRatio) + 0.25;
        _lineSupX = lineWidth > 0. ? (std::max(lineWidth, pixelAspectRatio) * renderScale.x / 2 / pixelAspectRatio - 0.25) : 0.;
        _lineInfY = std::max(0., lineWidth * renderScale.y / 2) + 0.25;
        _lineSupY = lineWidth > 0. ? (std::max(lineWidth, 1.) * renderScale.y / 2 - 0.25) : 0.;
        // always draw the centerline, whatever the render scale
        _centerlineColor = centerlineColor;
        _centerlineInfX = std::max(0., centerlineWidth * renderScale.x / 2 / pixelAspectRatio) + 0.25;
        _centerlineSupX = centerlineWidth > 0. ? (std::max(centerlineWidth * renderScale.x, pixelAspectRatio) / 2 / pixelAspectRatio - 0.25) : 0.;
        _centerlineInfY = std::max(0., centerlineWidth * renderScale.y / 2) + 0.25;
        _centerlineSupY = centerlineWidth > 0. ? (std::max(centerlineWidth * renderScale.y, 1.)  / 2 - 0.25) : 0.;
        _rod.x1 = rod.x1 * renderScale.x / pixelAspectRatio;
        _rod.x2 = rod.x2 * renderScale.x / pixelAspectRatio;
        _rod.y1 = rod.y1 * renderScale.y;
        _rod.y2 = rod.y2 * renderScale.y;
    }
};

/** @brief templated class to blend between two images */
template <class PIX, int nComponents, int max>
class CheckerBoardProcessor
    : public CheckerBoardProcessorBase
{
public:
    // ctor
    CheckerBoardProcessor(ImageEffect &instance)
        : CheckerBoardProcessorBase(instance)
    {
    }

private:
    static void colorToPIX(const OfxRGBAColourD& color,
                           PIX colorPix[nComponents])
    {
        float colorf[4];

        if (nComponents == 1) {
            // alpha
            colorf[0] = (float)color.a;
        } else if (nComponents == 2) {
            // xy
            colorf[0] = (float)color.r;
            colorf[1] = (float)color.g;
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
            if ( (nComponents == 3) || (nComponents == 4) ) {
                // don't delinearize alpha: it is always linear
                for (int c = 0; c < 3; ++c) {
                    if (max == 255) {
                        colorf[c] = Color::to_func_srgb(colorf[c]);
                    } else {
                        assert(max == 65535);
                        colorf[c] = Color::to_func_Rec709(colorf[c]);
                    }
                }
            }
            // clamp and convert to the destination type
            for (int c = 0; c < nComponents; ++c) {
                colorPix[c] = Color::floatToInt<max + 1>(colorf[c]);
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
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            // check if we are on the centerline
            if ( ( (center.y - _centerlineInfY) <= y ) && ( y < (center.y + _centerlineSupY) ) ) {
                for (int x = procWindow.x1; x < procWindow.x2; x++) {
                    for (int c = 0; c < nComponents; ++c) {
                        dstPix[c] = centerlineColor[c];
                    }
                    dstPix += nComponents;
                }
            } else {
                // the closest line between boxes
                double yline = center.y + _boxSize.y * std::floor( (y - center.y) / _boxSize.y + 0.5 );
                // check if we are on a line
                if ( ( (yline - _lineInfY) <= y ) && ( y < (yline + _lineSupY) ) ) {
                    for (int x = procWindow.x1; x < procWindow.x2; x++) {
                        for (int c = 0; c < nComponents; ++c) {
                            dstPix[c] = lineColor[c];
                        }
                        dstPix += nComponents;
                    }
                } else {
                    // draw boxes and vertical lines
                    int ybox = std::floor( (y - center.y) / _boxSize.y );
                    PIX *c0 = (ybox & 1) ? color3 : color0;
                    PIX *c1 = (ybox & 1) ? color2 : color1;

                    for (int x = procWindow.x1; x < procWindow.x2; x++) {
                        // check if we are on the centerline
                        if ( ( (center.x - _centerlineInfX) <= x ) && ( x < (center.x + _centerlineSupX) ) ) {
                            for (int c = 0; c < nComponents; ++c) {
                                dstPix[c] = centerlineColor[c];
                            }
                        } else {
                            // the closest line between boxes
                            double xline = center.x + _boxSize.x * std::floor( (x - center.x) / _boxSize.x + 0.5 );
                            // check if we are on a line
                            if ( ( (xline - _lineInfX) <= x ) && ( x < (xline + _lineSupX) ) ) {
                                for (int c = 0; c < nComponents; ++c) {
                                    dstPix[c] = lineColor[c];
                                }
                            } else {
                                // draw box
                                int xbox = std::floor( (x - center.x) / _boxSize.x );
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
    } // multiThreadProcessImages
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class CheckerBoardPlugin
    : public GeneratorPlugin
{
public:
    /** @brief ctor */
    CheckerBoardPlugin(OfxImageEffectHandle handle)
        : GeneratorPlugin(handle, true, kSupportsByte, kSupportsUShort, kSupportsHalf, kSupportsFloat)
        , _boxSize(NULL)
        , _color0(NULL)
        , _color1(NULL)
        , _color2(NULL)
        , _color3(NULL)
        , _lineColor(NULL)
        , _lineWidth(NULL)
        , _centerlineColor(NULL)
        , _centerlineWidth(NULL)
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
        assert(_size && _color0 && _color1 && _color2 && _color3 && _lineColor && _lineWidth && _centerlineColor && _centerlineWidth);
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    template <int nComponents>
    void renderInternal(const RenderArguments &args, BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(CheckerBoardProcessorBase &, const RenderArguments &args);

    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

private:
    Double2DParam *_boxSize;
    RGBAParam  *_color0;
    RGBAParam  *_color1;
    RGBAParam  *_color2;
    RGBAParam  *_color3;
    RGBAParam  *_lineColor;
    DoubleParam *_lineWidth;
    RGBAParam  *_centerlineColor;
    DoubleParam *_centerlineWidth;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


/* set up and run a processor */
void
CheckerBoardPlugin::setupAndProcess(CheckerBoardProcessorBase &processor,
                                    const RenderArguments &args)
{
    const double time = args.time;

    // get a dst image
    auto_ptr<Image>  dst( _dstClip->fetchImage(time) );

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

    // set the images
    processor.setDstImg( dst.get() );

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
    if ( !getRegionOfDefinition(time, rod) ) {
        OfxPointD siz = getProjectSize();
        OfxPointD off = getProjectOffset();
        rod.x1 = off.x;
        rod.x2 = off.x + siz.x;
        rod.y1 = off.y;
        rod.y2 = off.y + siz.y;
    }
    processor.setValues(args.renderScale, dst->getPixelAspectRatio(), boxSize, color0, color1, color2, color3, lineColor, lineWidth, centerlineColor, centerlineWidth, rod);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // CheckerBoardPlugin::setupAndProcess

// the internal render function
template <int nComponents>
void
CheckerBoardPlugin::renderInternal(const RenderArguments &args,
                                   BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case eBitDepthUByte: {
        CheckerBoardProcessor<unsigned char, nComponents, 255> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthUShort: {
        CheckerBoardProcessor<unsigned short, nComponents, 65535> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthFloat: {
        CheckerBoardProcessor<float, nComponents, 1> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
CheckerBoardPlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

#ifdef OFX_EXTENSIONS_NATRON
    assert(dstComponents == ePixelComponentRGBA || dstComponents == ePixelComponentRGB || dstComponents == ePixelComponentXY || dstComponents == ePixelComponentAlpha);
#else
    assert(dstComponents == ePixelComponentRGBA || dstComponents == ePixelComponentRGB || dstComponents == ePixelComponentAlpha);
#endif

    checkComponents(dstBitDepth, dstComponents);

    // do the rendering
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
}

void
CheckerBoardPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    // output is continuous
    clipPreferences.setOutputHasContinuousSamples(true);

    GeneratorPlugin::getClipPreferences(clipPreferences);
    //clipPreferences.setOutputPremultiplication(eImagePreMultiplied);
}

mDeclarePluginFactory(CheckerBoardPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
CheckerBoardPluginFactory::describe(ImageEffectDescriptor &desc)
{
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextGenerator);
    desc.addSupportedContext(eContextGeneral);
    if (kSupportsByte) {
        desc.addSupportedBitDepth(eBitDepthUByte);
    }
    if (kSupportsUShort) {
        desc.addSupportedBitDepth(eBitDepthUShort);
    }
    if (kSupportsFloat) {
        desc.addSupportedBitDepth(eBitDepthFloat);
    }

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderTwiceAlways(false);
    desc.setRenderThreadSafety(kRenderThreadSafety);

    generatorDescribe(desc);
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

void
CheckerBoardPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                             ContextEnum context)
{
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

    generatorDescribeInContext(page, desc, *dstClip, eGeneratorExtentDefault, ePixelComponentRGBA, true,  context);

#define kParamSize "boxsize"
#define kParamSizeLabel "Size"
#define kParamSizerHint "Size of the checkerboard boxes in pixels."

    // boxSize
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamBoxSize);
        param->setLabel(kParamBoxSizeLabel);
        param->setHint(kParamBoxSizeHint);
        param->setRange(1, 1, DBL_MAX, DBL_MAX);
        param->setDisplayRange(0, 0, 100, 100);
        param->setDoubleType(eDoubleTypeXY);
        param->setDefaultCoordinateSystem(eCoordinatesCanonical); // Nuke defaults to Normalized for XY and XYAbsolute!
        param->setDefault(64, 64);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }

    // color0
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamColor0);
        param->setLabel(kParamColor0Label);
        param->setHint(kParamColor0Hint);
        param->setDefault(0.1, 0.1, 0.1, 1.0);
        param->setRange(-DBL_MAX, -DBL_MAX, -DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX);
        param->setDisplayRange(0, 0, 0, 0, 1, 1, 1, 1);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }

    // color1
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamColor1);
        param->setLabel(kParamColor1Label);
        param->setHint(kParamColor1Hint);
        param->setDefault(0.5, 0.5, 0.5, 1.0);
        param->setRange(-DBL_MAX, -DBL_MAX, -DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX);
        param->setDisplayRange(0, 0, 0, 0, 1, 1, 1, 1);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }

    // color2
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamColor2);
        param->setLabel(kParamColor2Label);
        param->setHint(kParamColor2Hint);
        param->setDefault(0.1, 0.1, 0.1, 1.0);
        param->setRange(-DBL_MAX, -DBL_MAX, -DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX);
        param->setDisplayRange(0, 0, 0, 0, 1, 1, 1, 1);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }

    // color3
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamColor3);
        param->setLabel(kParamColor3Label);
        param->setHint(kParamColor3Hint);
        param->setDefault(0.5, 0.5, 0.5, 1.0);
        param->setRange(-DBL_MAX, -DBL_MAX, -DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX);
        param->setDisplayRange(0, 0, 0, 0, 1, 1, 1, 1);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }


    // linecolor
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamLineColor);
        param->setLabel(kParamLineColorLabel);
        param->setHint(kParamLineColorHint);
        param->setDefault(1.0, 1.0, 1.0, 1.0);
        param->setRange(-DBL_MAX, -DBL_MAX, -DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX);
        param->setDisplayRange(0, 0, 0, 0, 1, 1, 1, 1);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }

    // lineWidth
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamLineWidth);
        param->setLabel(kParamLineWidthLabel);
        param->setHint(kParamLineWidthHint);
        param->setDefault(0.);
        param->setRange(0., DBL_MAX);
        param->setDisplayRange(0, 10);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }


    // centerlineColor
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamCenterLineColor);
        param->setLabel(kParamCenterLineColorLabel);
        param->setHint(kParamCenterLineColorHint);
        param->setDefault(1.0, 1.0, 0.0, 1.0);
        param->setRange(-DBL_MAX, -DBL_MAX, -DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX);
        param->setDisplayRange(0, 0, 0, 0, 1, 1, 1, 1);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }

    // centerlineWidth
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamCenterLineWidth);
        param->setLabel(kParamCenterLineWidthLabel);
        param->setHint(kParamCenterLineWidthHint);
        param->setDefault(1);
        param->setRange(0., DBL_MAX);
        param->setDisplayRange(0, 10);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }
} // CheckerBoardPluginFactory::describeInContext

ImageEffect*
CheckerBoardPluginFactory::createInstance(OfxImageEffectHandle handle,
                                          ContextEnum /*context*/)
{
    return new CheckerBoardPlugin(handle);
}

static CheckerBoardPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
