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
 * OFX ColorBars plugin.
 */

#include <cmath>
#include <climits>
#include <cfloat>

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxsGenerator.h"
#include "ofxsLut.h"
#include "ofxsCoords.h"
#ifdef OFX_EXTENSIONS_NATRON
#include "ofxNatron.h"
#endif
#include "ofxsThreadSuite.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "ColorBarsOFX"
#define kPluginGrouping "Image"
#define kPluginDescription \
    "Generate an image with SMPTE RP 219:2002 color bars.\n" \
    "The output of this plugin is broadcast-safe of \"Output IRE\" is unchecked. Be careful that colorbars are defined in a nonlinear colorspace. In order to get linear RGB, this plug-in should be combined with a transformation from the video space to linear.\n" \
    "See also: http://opticalenquiry.com/nuke/index.php?title=Constant,_CheckerBoard,_ColorBars,_ColorWheel"

#define kPluginIdentifier "net.sf.openfx.ColorBars"
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

#define kParamBarIntensity "barIntensity"
#define kParamBarIntensityLabel "Bar Intensity"
#define kParamBarIntensityHint "Bar Intensity, in IRE unit."
#define kParamBarIntensityDefault 75

#define kParamOutputIRE "outputIRE"
#define kParamOutputIRELabel "Output IRE"
#define kParamOutputIREHint "When checked, the output is scaled so that 0 is black, the max value is white, and the superblack (under the middle of the magenta bar) has a negative value."


class ColorBarsProcessorBase
    : public ImageProcessor
{
protected:
    double _barIntensity;
    bool _outputIRE;
    OfxRectI _rod; // in pixel coordinates

public:
    /** @brief no arg ctor */
    ColorBarsProcessorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _barIntensity(kParamBarIntensityDefault)
        , _outputIRE(false)
        , _rod()
    {
        _rod.x1 = _rod.y1 = _rod.x2 = _rod.y2 = 0.;
    }

    void setValues(const double barIntensity,
                   const bool outputIRE,
                   const OfxRectI& rod)
    {
        _barIntensity = barIntensity;
        _outputIRE = outputIRE;
        _rod = rod;
    }
};

template <class PIX, int nComponents, int max>
class ColorBarsProcessor
    : public ColorBarsProcessorBase
{
public:
    // ctor
    ColorBarsProcessor(ImageEffect &instance)
        : ColorBarsProcessorBase(instance)
    {
        assert(nComponents >= 3);
    }

private:
    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        float ire[3];

        // push pixels
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            int yhd = (y - _rod.y1) * 1080 / (_rod.y2 - _rod.y1);
            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                int xhd = (x - _rod.x1) * 1920 / (_rod.x2 - _rod.x1);
                if (yhd < 270) { // bottom row (pluge)
                    if (xhd < 240) {            //  15 IRE
                        ire[0] = ire[1] = ire[2] =  15;
                    } else if (xhd < 548) {     //   0 IRE (black)
                        ire[0] = ire[1] = ire[2] =   0;
                    } else if (xhd < 960) {     // 100 IRE (white)
                        ire[0] = ire[1] = ire[2] = 100;
                    } else if (xhd < 1130) { //      0 IRE (black)
                        ire[0] = ire[1] = ire[2] =   0;
                    } else if (xhd < 1198) { //     -2 IRE (superblack)
                        ire[0] = ire[1] = ire[2] =  -2;
                    } else if (xhd < 1268) { //      0 IRE (black)
                        ire[0] = ire[1] = ire[2] =   0;
                    } else if (xhd < 1336) {    //   2 IRE
                        ire[0] = ire[1] = ire[2] =   2;
                    } else if (xhd < 1406) { //      0 IRE (black)
                        ire[0] = ire[1] = ire[2] =   0;
                    } else if (xhd < 1474) { //      4 IRE
                        ire[0] = ire[1] = ire[2] =   4;
                    } else if (xhd < 1680) {    //   0 IRE (black)
                        ire[0] = ire[1] = ire[2] =   0;
                    } else {                    //  15 IRE
                        ire[0] = ire[1] = ire[2] =  15;
                    }
                } else if (yhd < 360) { // row that starts with yellow, including the gradient ramp
                    if (xhd < 240) {            // 100,100,0 IRE (yellow)
                        ire[0] = ire[1] = 100; ire[2] = 0;
                    } else if (xhd < 446) {  //      0 IRE (black)
                        ire[0] = ire[1] = ire[2] =   0;
                    } else if (xhd < 1474) { //      gradient from 0 to 100 IRE
                        ire[0] = ire[1] = ire[2] = 100. * (xhd - 446) / float(1474 - 446);
                    } else if (xhd < 1680) {    // 100 IRE (white)
                        ire[0] = ire[1] = ire[2] = 100;
                    } else {                    // 100,0,0 IRE (red)
                        ire[0] = 100; ire[1] = ire[2] = 0;
                    }
                } else if (yhd < 450) { // row that starts with cyan
                    if (xhd < 240) {            // 0,100,100 IRE (cyan)
                        ire[0] = 0; ire[1] = ire[2] = 100;
                    } else if (xhd < 446) {  //    100 IRE (white)
                        ire[0] = ire[1] = ire[2] = 100;
                    } else if (xhd < 1680) { //     75 IRE
                        ire[0] = ire[1] = ire[2] = 75;
                    } else {                    // 0,0,100 IRE (blue)
                        ire[0] = ire[1] = 0; ire[2] = 100;
                    }
                } else { // colorbars
                    if (xhd < 240) {            //  40 IRE
                        ire[0] = ire[1] = ire[2] =  40;
                    } else if (xhd < 446) {     //  75 IRE
                        ire[0] = ire[1] = ire[2] =  75;
                    } else if (xhd < 652) {     // yellow
                        ire[0] = ire[1] = 75; ire[2] = 0;
                    } else if (xhd < 858) {     // cyan
                        ire[0] = 0; ire[1] = ire[2] = 75;
                    } else if (xhd < 1062) {    // green
                        ire[0] = 0; ire[1] = 75; ire[2] = 0;
                    } else if (xhd < 1268) {    // magenta
                        ire[0] = 75; ire[1] = 0; ire[2] = 75;
                    } else if (xhd < 1474) {    // red
                        ire[0] = 75; ire[1] = ire[2] = 0;
                    } else if (xhd < 1680) {    // blue
                        ire[0] = ire[1] = 0; ire[2] = 75;
                    } else {                    //  40 IRE
                        ire[0] = ire[1] = ire[2] =  40;
                    }
                    if (_barIntensity != 75) {
                        ire[0] *= _barIntensity / 75.;
                        ire[1] *= _barIntensity / 75.;
                        ire[2] *= _barIntensity / 75.;
                    }
                }
                if (_outputIRE) {
                    if (max > 1) {
                        for (int c = 0; c < 3; ++c) {
                            dstPix[c] = Color::floatToInt<max + 1>(ire[c] / 100.);
                        }
                    } else {
                        for (int c = 0; c < 3; ++c) {
                            dstPix[c] = ire[c] / 100.;
                        }
                    }
                } else {
                    if (max == 65535) {
                        for (int c = 0; c < 3; ++c) {
                            dstPix[c] = 4096 + Color::floatToInt<60160 - 4096>(ire[c] / 100.);
                        }
                    } else if (max == 255) {
                        for (int c = 0; c < 3; ++c) {
                            dstPix[c] = 16 + Color::floatToInt<235 - 16>(ire[c] / 100.);
                        }
                    } else {
                        for (int c = 0; c < 3; ++c) {
                            dstPix[c] = 0.0625 + (0.91796875 - 0.0625) * (ire[c] / 100.);
                        }
                    }
                }
                if (nComponents == 4) {
                    // set alpha
                    dstPix[nComponents - 1] = max;
                }
                dstPix += nComponents;
            }
        }
    } // multiThreadProcessImages
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ColorBarsPlugin
    : public GeneratorPlugin
{
public:
    /** @brief ctor */
    ColorBarsPlugin(OfxImageEffectHandle handle)
        : GeneratorPlugin(handle, true, kSupportsByte, kSupportsUShort, kSupportsHalf, kSupportsFloat)
    {
        _srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() == ePixelComponentRGBA ||
                             _srcClip->getPixelComponents() == ePixelComponentRGB ||
                             _srcClip->getPixelComponents() == ePixelComponentXY ||
                             _srcClip->getPixelComponents() == ePixelComponentAlpha) );
        _barIntensity = fetchDoubleParam(kParamBarIntensity);
        _outputIRE = fetchBooleanParam(kParamOutputIRE);
        assert(_barIntensity && _outputIRE);
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    template <int nComponents>
    void renderInternal(const RenderArguments &args, BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(ColorBarsProcessorBase &, const RenderArguments &args);

    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

private:
    DoubleParam* _barIntensity;
    BooleanParam* _outputIRE;
    Clip* _srcClip;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


/* set up and run a processor */
void
ColorBarsPlugin::setupAndProcess(ColorBarsProcessorBase &processor,
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

    OfxRectD rod = {0., 0., 0., 0.};
    if ( !getRegionOfDefinition(time, rod) ) {
        if ( _srcClip && _srcClip->isConnected() ) {
            rod = _srcClip->getRegionOfDefinition(time);
        } else {
            OfxPointD siz = getProjectSize();
            OfxPointD off = getProjectOffset();
            rod.x1 = off.x;
            rod.x2 = off.x + siz.x;
            rod.y1 = off.y;
            rod.y2 = off.y + siz.y;
        }
    }
    OfxRectI rod_pixel;
    Coords::toPixelEnclosing(rod, args.renderScale, dst->getPixelAspectRatio(), &rod_pixel);
    double barIntensity = _barIntensity->getValueAtTime(time);
    bool outputIRE = _outputIRE->getValueAtTime(time);
    processor.setValues(barIntensity, outputIRE, rod_pixel);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // ColorBarsPlugin::setupAndProcess

// the internal render function
template <int nComponents>
void
ColorBarsPlugin::renderInternal(const RenderArguments &args,
                                BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case eBitDepthUByte: {
        ColorBarsProcessor<unsigned char, nComponents, 255> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthUShort: {
        ColorBarsProcessor<unsigned short, nComponents, 65535> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthFloat: {
        ColorBarsProcessor<float, nComponents, 1> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
ColorBarsPlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert(dstComponents == ePixelComponentRGBA || dstComponents == ePixelComponentRGB || dstComponents == ePixelComponentXY || dstComponents == ePixelComponentAlpha);

    checkComponents(dstBitDepth, dstComponents);

    // do the rendering
    if (dstComponents == ePixelComponentRGBA) {
        renderInternal<4>(args, dstBitDepth);
    } else if (dstComponents == ePixelComponentRGB) {
        renderInternal<3>(args, dstBitDepth);
    }
}


void
ColorBarsPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    // output is continuous
    clipPreferences.setOutputHasContinuousSamples(true);

    GeneratorPlugin::getClipPreferences(clipPreferences);
    //clipPreferences.setOutputPremultiplication(eImagePreMultiplied);
}


mDeclarePluginFactory(ColorBarsPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
ColorBarsPluginFactory::describe(ImageEffectDescriptor &desc)
{
    desc.setLabel(kPluginName);
    desc.setPluginDescription(kPluginDescription);
    desc.setPluginGrouping(kPluginGrouping);
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
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentRGBA);
#endif

    generatorDescribe(desc);
}

void
ColorBarsPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                          ContextEnum context)
{
    // there has to be an input clip, even for generators
    ClipDescriptor* srcClip = desc.defineClip( kOfxImageEffectSimpleSourceClipName );

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setOptional(true);

    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->setSupportsTiles(kSupportsTiles);

    PageParamDescriptor *page = desc.definePageParam("Controls");

    generatorDescribeInContext(page, desc, *dstClip, eGeneratorExtentDefault, ePixelComponentRGBA, true, context);

    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamBarIntensity);
        param->setLabel(kParamBarIntensityLabel);
        param->setHint(kParamBarIntensityHint);
        param->setDefault(kParamBarIntensityDefault);
        param->setRange(0., 100.);
        param->setDisplayRange(0., 100.);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamOutputIRE);
        param->setLabel(kParamOutputIRELabel);
        param->setHint(kParamOutputIREHint);
        param->setDefault(false);
        if (page) {
            page->addChild(*param);
        }
    }
}

ImageEffect*
ColorBarsPluginFactory::createInstance(OfxImageEffectHandle handle,
                                       ContextEnum /*context*/)
{
    return new ColorBarsPlugin(handle);
}

static ColorBarsPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
