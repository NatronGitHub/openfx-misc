/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2016 INRIA
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
 * OFX ColorWheel plugin.
 */

#include <cmath>
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <windows.h>
#endif
#include <climits>
#include <cfloat> // DBL_MAX

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxsGenerator.h"
#include "ofxsLut.h"
#include "ofxsCoords.h"
#ifdef OFX_EXTENSIONS_NATRON
#include "ofxNatron.h"
#endif
using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "ColorWheelOFX"
#define kPluginGrouping "Image"
#define kPluginDescription "Generate an image with a color wheel.\n" \
"See also: http://opticalenquiry.com/nuke/index.php?title=Constant,_CheckerBoard,_ColorBars,_ColorWheel"
#define kPluginIdentifier "net.sf.openfx.ColorWheel"
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

#define kParamCenterSaturation "centerSaturation"
#define kParamCenterSaturationLabel "Center Saturation"
#define kParamCenterSaturationHint "Sets the HSV saturation level in the center of the color wheel."
#define kParamCenterSaturationDefault 0

#define kParamEdgeSaturation "edgeSaturation"
#define kParamEdgeSaturationLabel "Edge Saturation"
#define kParamEdgeSaturationHint "Sets the HSV saturation level at the edges of the color wheel."
#define kParamEdgeSaturationDefault 1

#define kParamCenterValue "centerValue"
#define kParamCenterValueLabel "Center Value"
#define kParamCenterValueHint "Sets the HSV value level in the center of the color wheel."
#define kParamCenterValueDefault 1

#define kParamEdgeValue "edgeValue"
#define kParamEdgeValueHint "Sets the HSV value level at the edges of the color wheel."
#define kParamEdgeValueLabel "Edge Value"
#define kParamEdgeValueDefault 1

#define kParamGamma "gamma"
#define kParamGammaLabel "Gamma"
#define kParamGammaHint "Sets the overall gamma level of the color wheel."
#define kParamGammaDefault 0.45

#define kParamRotate "rotate"
#define kParamRotateLabel "Rotate"
#define kParamRotateHint "Sets the amount of rotation to apply to color position in the color wheel. Negative values produce clockwise rotation and vice-versa."
#define kParamRotateDefault 0


class ColorWheelProcessorBase
    : public OFX::ImageProcessor
{
protected:
    double _centerSaturation;
    double _edgeSaturation;
    double _centerValue;
    double _edgeValue;
    double _gamma;
    double _rotate;
    OfxPointD _center; // in canonical coordinates
    double _radius;

public:
    /** @brief no arg ctor */
    ColorWheelProcessorBase(OFX::ImageEffect &instance)
        : OFX::ImageProcessor(instance)
        , _centerSaturation(0)
        , _edgeSaturation(0)
        , _centerValue(0)
        , _edgeValue(0)
        , _gamma(0)
        , _rotate(0)
        , _center()
        , _radius(1.)
    {
        _center.x = _center.y = 0.;
    }

    void setValues(const double centerSaturation,
                   const double edgeSaturation,
                   const double centerValue,
                   const double edgeValue,
                   const double gamma,
                   const double rotate,
                   const OfxPointD& center,
                   const double radius)
    {
        _centerSaturation = centerSaturation;
        _edgeSaturation = edgeSaturation;
        _centerValue = centerValue;
        _edgeValue = edgeValue;
        _gamma = gamma;
        _rotate = rotate - 360 * std::floor(rotate / 360.); // bring rotate between 0 and 360
        _center = center;
        _radius = radius;
    }
};

template <class PIX, int nComponents, int max>
class ColorWheelProcessor
    : public ColorWheelProcessorBase
{
public:
    // ctor
    ColorWheelProcessor(OFX::ImageEffect &instance)
        : ColorWheelProcessorBase(instance)
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
                        colorf[c] = OFX::Color::to_func_srgb(colorf[c]);
                    } else {
                        assert(max == 65535);
                        colorf[c] = OFX::Color::to_func_Rec709(colorf[c]);
                    }
                }
            }
            // clamp and convert to the destination type
            for (int c = 0; c < nComponents; ++c) {
                colorPix[c] = OFX::Color::floatToInt<max + 1>(colorf[c]);
            }
        }
    }

    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        OfxPointD renderScale = _dstImg->getRenderScale();
        double par = _dstImg->getPixelAspectRatio();

        // push pixels
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                OfxPointI p_pixel = {x, y};
                OfxPointD p_canonical;
                OFX::Coords::toCanonical(p_pixel, renderScale, par, &p_canonical);
                double r2 = ( (p_canonical.x - _center.x) * (p_canonical.x - _center.x) +
                              (p_canonical.y - _center.y) * (p_canonical.y - _center.y) );
                if (r2 > _radius * _radius) {
                    for (int c = 0; c < nComponents; ++c) {
                        dstPix[c] = 0;
                    }
                } else {
                    // hue in [0..1]
                    double r1 = std::sqrt(r2);
                    double hue = r1 > 0. ? OFXS_HUE_CIRCLE * std::acos( std::max( -1., std::min( (p_canonical.x - _center.x) / r1, 1. ) ) ) / (2 * M_PI) : 0.;
                    assert(hue == hue);
                    if (p_canonical.y > _center.y) {
                        hue = OFXS_HUE_CIRCLE - hue;
                    }
                    hue += _rotate / 360;
                    hue = hue - std::floor(hue / OFXS_HUE_CIRCLE) * OFXS_HUE_CIRCLE;
                    assert(hue >= 0. && hue <= OFXS_HUE_CIRCLE);
                    double a = r1 / _radius;
                    double saturation = _centerSaturation + a * (_edgeSaturation - _centerSaturation);
                    double value = _centerValue + a * (_edgeValue - _centerValue);
                    float r, g, b;
                    //r = hue; g = saturation; b = value;
                    OFX::Color::hsv_to_rgb(hue, saturation, value, &r, &g, &b);
                    OfxRGBAColourD color = {r, g, b, 1.};
                    if (_gamma <= 0.) {
                        color.r = color.r >= 1. ? 1 : 0.;
                        color.g = color.g >= 1. ? 1 : 0.;
                        color.b = color.b >= 1. ? 1 : 0.;
                    } else if (_gamma != 1.) {
                        color.r = std::pow(color.r, 1. / _gamma);
                        color.g = std::pow(color.g, 1. / _gamma);
                        color.b = std::pow(color.b, 1. / _gamma);
                    }
                    colorToPIX(color, dstPix);
                }
                dstPix += nComponents;
            }
        }
    } // multiThreadProcessImages
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ColorWheelPlugin
    : public GeneratorPlugin
{
public:
    /** @brief ctor */
    ColorWheelPlugin(OfxImageEffectHandle handle)
        : GeneratorPlugin(handle, true, kSupportsByte, kSupportsUShort, kSupportsHalf, kSupportsFloat)
        , _centerSaturation(0)
        , _edgeSaturation(0)
        , _centerValue(0)
        , _edgeValue(0)
        , _gamma(0)
        , _rotate(0)
    {
        _srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() == OFX::ePixelComponentRGBA ||
                             _srcClip->getPixelComponents() == OFX::ePixelComponentRGB ||
                             _srcClip->getPixelComponents() == OFX::ePixelComponentXY ||
                             _srcClip->getPixelComponents() == OFX::ePixelComponentAlpha) );
        _centerSaturation = fetchDoubleParam(kParamCenterSaturation);
        _edgeSaturation = fetchDoubleParam(kParamEdgeSaturation);
        _centerValue = fetchDoubleParam(kParamCenterValue);
        _edgeValue = fetchDoubleParam(kParamEdgeValue);
        _gamma = fetchDoubleParam(kParamGamma);
        _rotate = fetchDoubleParam(kParamRotate);
        assert(_centerSaturation && _edgeSaturation && _centerValue && _edgeValue && _gamma && _rotate);
    }

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    template <int nComponents>
    void renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(ColorWheelProcessorBase &, const OFX::RenderArguments &args);

    //virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;


private:
    DoubleParam* _centerSaturation;
    DoubleParam* _edgeSaturation;
    DoubleParam* _centerValue;
    DoubleParam* _edgeValue;
    DoubleParam* _gamma;
    DoubleParam* _rotate;
    Clip* _srcClip;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


/* set up and run a processor */
void
ColorWheelPlugin::setupAndProcess(ColorWheelProcessorBase &processor,
                                  const OFX::RenderArguments &args)
{
    const double time = args.time;
    // get a dst image
    std::auto_ptr<OFX::Image>  dst( _dstClip->fetchImage(time) );

    if ( !dst.get() ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    // set the images
    processor.setDstImg( dst.get() );

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    double centerSaturation = _centerSaturation->getValueAtTime(time);
    double edgeSaturation = _edgeSaturation->getValueAtTime(time);
    double centerValue = _centerValue->getValueAtTime(time);
    double edgeValue = _edgeValue->getValueAtTime(time);
    double gamma = _gamma->getValueAtTime(time);
    double rotate = _rotate->getValueAtTime(time);
    OfxPointD center = {0., 0.};
    double radius = 1.;
    OfxRectD rod = {0., 0., 0., 0.};
    if ( !getRegionOfDefinition(rod) ) {
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
    center.x = (rod.x2 - rod.x1) / 2;
    center.y = (rod.y2 - rod.y1) / 2;
    radius = std::min( (rod.x2 - rod.x1) / 2, (rod.y2 - rod.y1) / 2 );
    processor.setValues(centerSaturation, edgeSaturation, centerValue, edgeValue, gamma, rotate, center, radius);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // ColorWheelPlugin::setupAndProcess

// the internal render function
template <int nComponents>
void
ColorWheelPlugin::renderInternal(const OFX::RenderArguments &args,
                                 OFX::BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case OFX::eBitDepthUByte: {
        ColorWheelProcessor<unsigned char, nComponents, 255> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case OFX::eBitDepthUShort: {
        ColorWheelProcessor<unsigned short, nComponents, 65535> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case OFX::eBitDepthFloat: {
        ColorWheelProcessor<float, nComponents, 1> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    default:
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
ColorWheelPlugin::render(const OFX::RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert(dstComponents == OFX::ePixelComponentRGBA || dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentXY || dstComponents == OFX::ePixelComponentAlpha);

    checkComponents(dstBitDepth, dstComponents);

    // do the rendering
    if (dstComponents == OFX::ePixelComponentRGBA) {
        renderInternal<4>(args, dstBitDepth);
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        renderInternal<3>(args, dstBitDepth);
    } else if (dstComponents == OFX::ePixelComponentXY) {
        renderInternal<2>(args, dstBitDepth);
    } else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        renderInternal<1>(args, dstBitDepth);
    }
}

//void
//ColorWheelPlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
//{
//    GeneratorPlugin::getClipPreferences(clipPreferences);
//    clipPreferences.setOutputPremultiplication(OFX::eImagePreMultiplied);
//}


mDeclarePluginFactory(ColorWheelPluginFactory, {}, {});
void
ColorWheelPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
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
ColorWheelPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                           ContextEnum context)
{
    // there has to be an input clip, even for generators
    ClipDescriptor* srcClip = desc.defineClip( kOfxImageEffectSimpleSourceClipName );

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentXY);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setOptional(true);

    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentXY);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    PageParamDescriptor *page = desc.definePageParam("Controls");

    generatorDescribeInContext(page, desc, *dstClip, eGeneratorExtentDefault, ePixelComponentRGBA, true, context);

    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamCenterSaturation);
        param->setLabel(kParamCenterSaturationLabel);
        param->setHint(kParamCenterSaturationHint);
        param->setDefault(kParamCenterSaturationDefault);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamEdgeSaturation);
        param->setLabel(kParamEdgeSaturationLabel);
        param->setHint(kParamEdgeSaturationHint);
        param->setDefault(kParamEdgeSaturationDefault);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamCenterValue);
        param->setLabel(kParamCenterValueLabel);
        param->setHint(kParamCenterValueHint);
        param->setDefault(kParamCenterValueDefault);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamEdgeValue);
        param->setLabel(kParamEdgeValueLabel);
        param->setHint(kParamEdgeValueHint);
        param->setDefault(kParamEdgeValueDefault);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamGamma);
        param->setLabel(kParamGammaLabel);
        param->setHint(kParamGammaHint);
        param->setDefault(kParamGammaDefault);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamRotate);
        param->setLabel(kParamRotateLabel);
        param->setHint(kParamRotateHint);
        param->setDefault(kParamRotateDefault);
        param->setRange(-DBL_MAX, DBL_MAX);
        param->setDisplayRange(-180., 180.);
        if (page) {
            page->addChild(*param);
        }
    }
} // ColorWheelPluginFactory::describeInContext

ImageEffect*
ColorWheelPluginFactory::createInstance(OfxImageEffectHandle handle,
                                        ContextEnum /*context*/)
{
    return new ColorWheelPlugin(handle);
}

static ColorWheelPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
