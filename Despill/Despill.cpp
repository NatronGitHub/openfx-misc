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
 * OFX Despill plugin.
 */

#include <cmath>
#include <algorithm>

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsCopier.h"
#include "ofxsMacros.h"
#include "ofxsCoords.h"
#include "ofxNatron.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "Despill"
#define kPluginGrouping "Keyer"
#define kPluginDescription "Remove the unwanted color contamination of the foreground (spill) " \
    "caused by the reflected color of the bluescreen/greenscreen.\n" \
    "While a despill operation often only removes green (for greenscreens) this despill also enables adding red and blue to the spill area. " \
    "A lot of Keyers already have implemented their own despill methods. " \
    "However, in a lot of cases it is useful to seperate the keying process in 2 tasks to get more control over the final result. " \
    "Normally these tasks are the generation of the alpha mask and the spill correction. " \
    "The generated alpha Mask (Key) is then used to merge the despilled forground over the new background.\n" \
    "This effect is based on the unspill operations described in section 4.5 of \"Digital Compositing for Film and Video\" by Steve Wright (Focal Press)."

#define kPluginIdentifier "net.sf.openfx.Despill"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamScreenType "screenType"
#define kParamScreenTypeLabel "Screen Type"
#define kParamScreenTypeHint "Select the screen type according to your footage"
#define kParamScreenTypeOptionGreen "Greenscreen"
#define kParamScreenTypeOptionBlue "Bluescreen"
enum ScreenTypeEnum
{
    eScreenTypeGreenScreen,
    eScreenTypeBlueScreen
};

#define kParamSpillMapMix "spillmapMix"
#define kParamSpillMapMixLabel "Spillmap Mix"
#define kParamSpillMapMixHint "This value controls the generation of the spillmap.\n" \
    "The spillmap decides in which areas the spill will be removed.\n" \
    "To calculate this map the two none screen colors are combined according to this value and then subtracted from the screen color.\n" \
    "Greenscreen:\n" \
    "0: limit green by blue\n" \
    "0,5: limit green by the average of red and blue\n" \
    "1:  limit green by red\n" \
    "Bluescreen:\n" \
    "0: limit blue by green\n" \
    "0,5: limit blue by the average of red and green\n" \
    "1:  limit blue by red\n" \

#define kParamExpandSpillMap "expandSpillmap"
#define kParamExpandSpillMapLabel "Expand Spillmap"
#define kParamExpandSpillMapHint "This will expand the spillmap to get rid of still remaining spill.\n" \
    "It works by lowering the values that will be subtracted from green or blue."

#define kParamOutputSpillMap "outputSpillMap"
#define kParamOutputSpillMapLabel "Spillmap to Alpha"
#define kParamOutputSpillMapHint "If checked, this will output the spillmap in the alpha channel."

#define kParamScaleRed "scaleRed"
#define kParamScaleRedLabel "Red Scale"
#define kParamScaleRedHint "Controls the amount of Red in the spill area"

#define kParamScaleGreen "scaleGreen"
#define kParamScaleGreenLabel "Green Scale"
#define kParamScaleGreenHint "Controls the amount of Green in the spill area.\n This value should be negative for greenscreen footage."

#define kParamScaleBlue "scaleBlue"
#define kParamScaleBlueLabel "Blue Scale"
#define kParamScaleBlueHint "Controls the amount of Blue in the spill area.\n This value should be negative for bluescreen footage."

#define kParamBrightness "brightness"
#define kParamBrightnessLabel "Brightness"
#define kParamBrightnessHint "Controls the brightness of the spill while trying to preserve the colors."


class DespillProcessorBase
    : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImg;
    const OFX::Image *_maskImg;
    bool _maskInvert;
    bool _outputToAlpha;
    double _spillMix;
    double _spillExpand;
    double _redScale, _greenScale, _blueScale;
    double _brightness;
    double _mix;

public:

    DespillProcessorBase(OFX::ImageEffect &instance)
        : OFX::ImageProcessor(instance)
        , _srcImg(0)
        , _maskImg(0)
        , _maskInvert(false)
        , _outputToAlpha(false)
        , _spillMix(0)
        , _spillExpand(0)
        , _redScale(0)
        , _greenScale(0)
        , _blueScale(0)
        , _brightness(0)
        , _mix(0)
    {
    }

    void setMaskImg(const OFX::Image* m,
                    bool maskInvert)
    {
        _maskImg = m;
        _maskInvert = maskInvert;
    }

    void setSrcImg(const OFX::Image *v)
    {
        _srcImg = v;
    }

    void setValues(double spillMix,
                   double spillExpand,
                   double red,
                   double green,
                   double blue,
                   double brightness,
                   double mix,
                   bool outputToAlpha)
    {
        _spillMix = spillMix;
        _spillExpand = spillExpand;
        _redScale = red;
        _greenScale = green;
        _blueScale = blue;
        _brightness = brightness;
        _mix = mix;
        _outputToAlpha = outputToAlpha;
    }

private:
};


template <class PIX, int nComponents, int maxValue, ScreenTypeEnum screen>
class DespillProcessor
    : public DespillProcessorBase
{
public:
    DespillProcessor(OFX::ImageEffect &instance)
        : DespillProcessorBase(instance)
    {
    }

private:

    void multiThreadProcessImages(OfxRectI procWindow)
    {
        float tmpPix[4];

        assert(nComponents == 3 || nComponents == 4);
        assert(_dstImg);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                double spillmap;
                if (srcPix) {
                    tmpPix[0] = (double)srcPix[0] / maxValue;
                    tmpPix[1] = (double)srcPix[1] / maxValue;
                    tmpPix[2] = (double)srcPix[2] / maxValue;
                    tmpPix[3] = (double)srcPix[3] / maxValue;
                    if (screen == eScreenTypeGreenScreen) {
                        spillmap = std::max(tmpPix[1] - ( tmpPix[0] * _spillMix + tmpPix[2] * (1 - _spillMix) ) * (1 - _spillExpand), 0.);
                    } else {
                        spillmap = std::max(tmpPix[2] - ( tmpPix[0] * _spillMix + tmpPix[1] * (1 - _spillMix) ) * (1 - _spillExpand), 0.);
                    }

                    tmpPix[0] = std::max(tmpPix[0] + spillmap * _redScale + _brightness * spillmap, 0.);
                    tmpPix[1] = std::max(tmpPix[1] + spillmap * _greenScale + _brightness * spillmap, 0.);
                    tmpPix[2] = std::max(tmpPix[2] + spillmap * _blueScale + _brightness * spillmap, 0.);
                } else {
                    tmpPix[0] = tmpPix[1] = tmpPix[2] = tmpPix[3] = 0.;
                    spillmap = 0.;
                }

                ofxsMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, x, y, srcPix, _maskImg != 0, _maskImg, _mix, _maskInvert, dstPix);

                if (_outputToAlpha) {
                    assert(nComponents == 4);
                    dstPix[3] = ofxsClampIfInt<PIX, maxValue>(spillmap * maxValue, 0, maxValue);
                }

                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    }
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class DespillPlugin
    : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    DespillPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(0)
        , _srcClip(0)
        , _maskClip(0)
        , _screenType(0)
        , _spillMix(0)
        , _expandSpill(0)
        , _outputToAlpha(0)
        , _redScale(0)
        , _greenScale(0)
        , _blueScale(0)
        , _brightness(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (_dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == OFX::eContextGenerator) ||
                ( _srcClip && (_srcClip->getPixelComponents() == ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA) ) );
        _maskClip = fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || _maskClip->getPixelComponents() == ePixelComponentAlpha);

        _screenType = fetchChoiceParam(kParamScreenType);
        _spillMix = fetchDoubleParam(kParamSpillMapMix);
        _expandSpill = fetchDoubleParam(kParamExpandSpillMap);
        _outputToAlpha = fetchBooleanParam(kParamOutputSpillMap),
        _redScale = fetchDoubleParam(kParamScaleRed);
        _greenScale = fetchDoubleParam(kParamScaleGreen);
        _blueScale = fetchDoubleParam(kParamScaleBlue);
        _brightness = fetchDoubleParam(kParamBrightness);

        _mix = fetchDoubleParam(kParamMix);
        _maskApply = paramExists(kParamMaskApply) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
    }

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /** @brief get the clip preferences */
    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(DespillProcessorBase &, const OFX::RenderArguments &args);

private:


    template<int nComponents>
    void renderForComponents(const OFX::RenderArguments &args);

    template <class PIX, int nComponents, int maxValue>
    void renderForBitDepth(const OFX::RenderArguments &args);

    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    Clip *_maskClip;
    ChoiceParam* _screenType;
    DoubleParam* _spillMix;
    DoubleParam* _expandSpill;
    BooleanParam* _outputToAlpha;
    DoubleParam* _redScale;
    DoubleParam* _greenScale;
    DoubleParam* _blueScale;
    DoubleParam* _brightness;
    DoubleParam* _mix;
    BooleanParam* _maskApply;
    BooleanParam* _maskInvert;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
DespillPlugin::setupAndProcess(DespillProcessorBase &processor,
                               const OFX::RenderArguments &args)
{
    std::auto_ptr<OFX::Image> dst( _dstClip->fetchImage(args.time) );

    if ( !dst.get() ) {
        setPersistentMessage(OFX::Message::eMessageError, "", "Failed to fetch output image");
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
    std::auto_ptr<const OFX::Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                         _srcClip->fetchImage(args.time) : 0 );

    if ( src.get() ) {
        if ( (src->getRenderScale().x != args.renderScale.x) ||
             ( src->getRenderScale().y != args.renderScale.y) ||
             ( ( src->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( src->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum srcBitDepth      = src->getPixelDepth();
        //OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if (srcBitDepth != dstBitDepth /* || srcComponents != dstComponents*/) { // Keyer outputs RGBA but may have RGB input
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    } else {
        setPersistentMessage(OFX::Message::eMessageError, "", "Failed to fetch source image");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }


    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(args.time) ) && _maskClip && _maskClip->isConnected() );
    std::auto_ptr<const OFX::Image> mask(doMasking ? _maskClip->fetchImage(args.time) : 0);
    if ( mask.get() ) {
        if ( (mask->getRenderScale().x != args.renderScale.x) ||
             ( mask->getRenderScale().y != args.renderScale.y) ||
             ( ( mask->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( mask->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }


    // set the images
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );


    double spillMix;
    _spillMix->getValue(spillMix);
    double spillExpand;
    _expandSpill->getValue(spillExpand);
    bool outputAlpha;
    _outputToAlpha->getValue(outputAlpha);

    double redScale, greenScale, blueScale, brightNess, mix;
    _redScale->getValue(redScale);
    _greenScale->getValue(greenScale);
    _blueScale->getValue(blueScale);
    _brightness->getValue(brightNess);
    _mix->getValue(mix);


    if ( outputAlpha && (dst->getPixelComponentCount() != 4) ) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }


    processor.setValues(spillMix, spillExpand, redScale, greenScale, blueScale, brightNess, mix, outputAlpha);

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // DespillPlugin::setupAndProcess

// the overridden render function
void
DespillPlugin::render(const OFX::RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    // do the rendering
    if (dstComponents == OFX::ePixelComponentRGBA) {
        renderForComponents<4>(args);
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        renderForComponents<3>(args);
    } else if (dstComponents == OFX::ePixelComponentXY) {
        renderForComponents<2>(args);
    }  else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        renderForComponents<1>(args);
    } // switch
} // render

template<int nComponents>
void
DespillPlugin::renderForComponents(const OFX::RenderArguments &args)
{
    OFX::BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();

    switch (dstBitDepth) {
    case OFX::eBitDepthUByte:
        renderForBitDepth<unsigned char, nComponents, 255>(args);
        break;

    case OFX::eBitDepthUShort:
        renderForBitDepth<unsigned short, nComponents, 65535>(args);
        break;

    case OFX::eBitDepthFloat:
        renderForBitDepth<float, nComponents, 1>(args);
        break;
    default:
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

template <class PIX, int nComponents, int maxValue>
void
DespillPlugin::renderForBitDepth(const OFX::RenderArguments &args)
{
    ScreenTypeEnum s = (ScreenTypeEnum)_screenType->getValue();

    switch (s) {
    case eScreenTypeGreenScreen: {
        DespillProcessor<PIX, nComponents, maxValue, eScreenTypeGreenScreen> fred(*this);
        setupAndProcess(fred, args);
        break;
    }

    case eScreenTypeBlueScreen: {
        DespillProcessor<PIX, nComponents, maxValue, eScreenTypeBlueScreen> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    }
}

/* Override the clip preferences */
void
DespillPlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
    bool createAlpha;

    _outputToAlpha->getValue(createAlpha);
    if (createAlpha) {
        clipPreferences.setClipComponents(*_dstClip, OFX::ePixelComponentRGBA);
    }
}

mDeclarePluginFactory(DespillPluginFactory, {}, {} );
void
DespillPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // Say we are a transition context
    desc.addSupportedContext(eContextTransition);
    desc.addSupportedContext(eContextGeneral);

    // Add supported pixel depths
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
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentRGBA);
#endif
}

void
DespillPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                        ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->setSupportsTiles(kSupportsTiles);


    ClipDescriptor *maskClip = desc.defineClip("Mask");
    maskClip->addSupportedComponent(ePixelComponentAlpha);
    maskClip->setTemporalClipAccess(false);
    if (context != eContextPaint) {
        maskClip->setOptional(true);
    }
    maskClip->setSupportsTiles(kSupportsTiles);
    maskClip->setIsMask(true);


    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamScreenType);
        param->setLabel(kParamScreenTypeLabel);
        param->setHint(kParamScreenTypeHint);
        assert(param->getNOptions() == eScreenTypeGreenScreen);
        param->appendOption(kParamScreenTypeOptionGreen);
        assert(param->getNOptions() == eScreenTypeBlueScreen);
        param->appendOption(kParamScreenTypeOptionBlue);
        param->setDefault( (int)eScreenTypeGreenScreen );
        if (page) {
            page->addChild(*param);
        }
    }

    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSpillMapMix);
        param->setLabel(kParamSpillMapMixLabel);
        param->setHint(kParamSpillMapMixHint);
        param->setRange(0., 1.);
        param->setDefault(0.5);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamExpandSpillMap);
        param->setLabel(kParamExpandSpillMapLabel);
        param->setHint(kParamExpandSpillMapHint);
        param->setRange(0., 1.);
        param->setDefault(0.);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamOutputSpillMap);
        param->setLabel(kParamOutputSpillMapLabel);
        param->setHint(kParamOutputSpillMapHint);
        param->setDefault(false);
        param->setLayoutHint(eLayoutHintDivider, 0);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }


    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamScaleRed);
        param->setLabel(kParamScaleRedLabel);
        param->setHint(kParamScaleRedHint);
        param->setRange(-100., 100.);
        param->setDisplayRange(-2., 2.);
        param->setDefault(0.);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamScaleGreen);
        param->setLabel(kParamScaleGreenLabel);
        param->setHint(kParamScaleGreenHint);
        param->setRange(-100., 100.);
        param->setDisplayRange(-2., 2.);
        param->setDefault(-1.);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamScaleBlue);
        param->setLabel(kParamScaleBlueLabel);
        param->setHint(kParamScaleBlueHint);
        param->setRange(-100., 100.);
        param->setDisplayRange(-2., 2.);
        param->setDefault(0.);
        param->setLayoutHint(eLayoutHintDivider, 0);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamBrightness);
        param->setLabel(kParamBrightnessLabel);
        param->setHint(kParamBrightnessHint);
        param->setRange(-10., 10.);
        param->setDisplayRange(-1., 1.);
        param->setDefault(0.);
        if (page) {
            page->addChild(*param);
        }
    }


    ofxsMaskMixDescribeParams(desc, page);
} // DespillPluginFactory::describeInContext

ImageEffect*
DespillPluginFactory::createInstance(OfxImageEffectHandle handle,
                                     ContextEnum /*context*/)
{
    return new DespillPlugin(handle);
}

static DespillPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
