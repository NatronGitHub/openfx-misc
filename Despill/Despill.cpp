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
 * OFX Despill plugin.
 */

#include <cmath>
#include <algorithm>

#include "ofxsImageEffect.h"
#include "ofxsThreadSuite.h"
#include "ofxsMultiThread.h"

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsCopier.h"
#include "ofxsMacros.h"
#include "ofxsCoords.h"
#ifdef OFX_EXTENSIONS_NATRON
#include "ofxNatron.h"
#endif
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
#define kParamScreenTypeOptionGreen "Greenscreen", "The background screen has a green tint.", "green"
#define kParamScreenTypeOptionBlue "Bluescreen", "The background screen has a blue tint.", "blue"
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

#define kParamClampBlack "clampBlack"
#define kParamClampBlackLabel "Clamp Black"
#define kParamClampBlackHint "All colors below 0 on output are set to 0."

#define kParamClampWhite "clampWhite"
#define kParamClampWhiteLabel "Clamp White"
#define kParamClampWhiteHint "All colors above 1 on output are set to 1."

class DespillProcessorBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg;
    const Image *_maskImg;
    bool _maskInvert;
    bool _outputToAlpha;
    double _spillMix;
    double _spillExpand;
    double _redScale, _greenScale, _blueScale;
    double _brightness;
    bool _clampBlack;
    bool _clampWhite;
    float _mix;

public:

    DespillProcessorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _srcImg(NULL)
        , _maskImg(NULL)
        , _maskInvert(false)
        , _outputToAlpha(false)
        , _spillMix(0.)
        , _spillExpand(0.)
        , _redScale(0.)
        , _greenScale(0.)
        , _blueScale(0.)
        , _brightness(0.)
        , _clampBlack(true)
        , _clampWhite(false)
        , _mix(1.f)
    {
    }

    void setMaskImg(const Image* m,
                    bool maskInvert)
    {
        _maskImg = m;
        _maskInvert = maskInvert;
    }

    void setSrcImg(const Image *v)
    {
        _srcImg = v;
    }

    void setValues(double spillMix,
                   double spillExpand,
                   double red,
                   double green,
                   double blue,
                   double brightness,
                   bool clampBlack,
                   bool clampWhite,
                   float mix,
                   bool outputToAlpha)
    {
        _spillMix = spillMix;
        _spillExpand = spillExpand;
        _redScale = red;
        _greenScale = green;
        _blueScale = blue;
        _brightness = brightness;
        _clampBlack = clampBlack;
        _clampWhite = clampWhite;
        _mix = mix;
        _outputToAlpha = outputToAlpha;
    }

protected:
    // clamp for integer PIX types
    template<class PIX>
    float clamp(float value,
                int maxValue) const
    {
        return (std::max)( 0.f, (std::min)( value, float(maxValue) ) );
    }

    // clamp for integer PIX types
    template<class PIX>
    double clamp(double value,
                 int maxValue) const
    {
        return (std::max)( 0., (std::min)( value, double(maxValue) ) );
    }

private:
};

// floats don't clamp except if _clampBlack or _clampWhite
template<>
float
DespillProcessorBase::clamp<float>(float value,
                                   int maxValue) const
{
    assert(maxValue == 1.);
    if ( _clampBlack && (value < 0.) ) {
        value = 0.f;
    } else if ( _clampWhite && (value > 1.0) ) {
        value = 1.0f;
    }

    return value;
}

template<>
double
DespillProcessorBase::clamp<float>(double value,
                                   int maxValue) const
{
    assert(maxValue == 1.);
    if ( _clampBlack && (value < 0.) ) {
        value = 0.f;
    } else if ( _clampWhite && (value > 1.0) ) {
        value = 1.0f;
    }

    return value;
}

template<class PIX, int maxValue>
static float
sampleToFloat(PIX value)
{
    return (maxValue == 1) ? value : (value / (float)maxValue);
}

template <class PIX, int nComponents, int maxValue, ScreenTypeEnum screen>
class DespillProcessor
    : public DespillProcessorBase
{
public:
    DespillProcessor(ImageEffect &instance)
        : DespillProcessorBase(instance)
    {
    }

private:

    void multiThreadProcessImages(OfxRectI procWindow)
    {
        float tmpPix[4];

        assert(nComponents == 3 || nComponents == 4);
        assert(_dstImg);
        if (!_dstImg) {
            return;
        }
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            assert(dstPix);
            if (!dstPix) {
                continue;
            }
            const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(procWindow.x1, y) : 0);

            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                double spillmap;
                if (srcPix) {
                    tmpPix[0] = sampleToFloat<PIX, maxValue>(srcPix[0]);
                    tmpPix[1] = sampleToFloat<PIX, maxValue>(srcPix[1]);
                    tmpPix[2] = sampleToFloat<PIX, maxValue>(srcPix[2]);
                    if (nComponents == 4) {
                        tmpPix[3] = sampleToFloat<PIX, maxValue>(srcPix[3]);
                    } else {
                        tmpPix[3] = 0.;
                    }
                    if (screen == eScreenTypeGreenScreen) {
                        spillmap = (std::max)(tmpPix[1] - ( tmpPix[0] * _spillMix + tmpPix[2] * (1 - _spillMix) ) * (1 - _spillExpand), 0.);
                    } else {
                        spillmap = (std::max)(tmpPix[2] - ( tmpPix[0] * _spillMix + tmpPix[1] * (1 - _spillMix) ) * (1 - _spillExpand), 0.);
                    }

                    tmpPix[0] = clamp<float>(tmpPix[0] + spillmap * _redScale   + _brightness * spillmap, 1.);
                    tmpPix[1] = clamp<float>(tmpPix[1] + spillmap * _greenScale + _brightness * spillmap, 1.);
                    tmpPix[2] = clamp<float>(tmpPix[2] + spillmap * _blueScale  + _brightness * spillmap, 1.);
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
                srcPix += nComponents;
            }
        }
    } // multiThreadProcessImages
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class DespillPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    DespillPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _maskClip(NULL)
        , _screenType(NULL)
        , _spillMix(NULL)
        , _expandSpill(NULL)
        , _outputToAlpha(NULL)
        , _redScale(NULL)
        , _greenScale(NULL)
        , _blueScale(NULL)
        , _brightness(NULL)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA) ) );
        _maskClip = fetchClip(getContext() == eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || !_maskClip->isConnected() || _maskClip->getPixelComponents() == ePixelComponentAlpha);

        _screenType = fetchChoiceParam(kParamScreenType);
        _spillMix = fetchDoubleParam(kParamSpillMapMix);
        _expandSpill = fetchDoubleParam(kParamExpandSpillMap);
        _outputToAlpha = fetchBooleanParam(kParamOutputSpillMap),
        _redScale = fetchDoubleParam(kParamScaleRed);
        _greenScale = fetchDoubleParam(kParamScaleGreen);
        _blueScale = fetchDoubleParam(kParamScaleBlue);
        _brightness = fetchDoubleParam(kParamBrightness);
        _clampBlack = fetchBooleanParam(kParamClampBlack);
        _clampWhite = fetchBooleanParam(kParamClampWhite);
        assert(_clampBlack && _clampWhite);

        _mix = fetchDoubleParam(kParamMix);
        _maskApply = ( ofxsMaskIsAlwaysConnected( OFX::getImageEffectHostDescription() ) && paramExists(kParamMaskApply) ) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
    }

    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /** @brief get the clip preferences */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(DespillProcessorBase &, const RenderArguments &args);

private:


    template<int nComponents>
    void renderForComponents(const RenderArguments &args);

    template <class PIX, int nComponents, int maxValue>
    void renderForBitDepth(const RenderArguments &args);

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
    BooleanParam* _clampBlack;
    BooleanParam* _clampWhite;
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
                               const RenderArguments &args)
{
    const double time = args.time;
    auto_ptr<Image> dst( _dstClip->fetchImage(time) );

    if ( !dst.get() ) {
        setPersistentMessage(Message::eMessageError, "", "Failed to fetch output image");
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
    auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                    _srcClip->fetchImage(time) : 0 );

    if ( src.get() ) {
        if ( (src->getRenderScale().x != args.renderScale.x) ||
             ( src->getRenderScale().y != args.renderScale.y) ||
             ( ( src->getField() != eFieldNone) /* for DaVinci Resolve */ && ( src->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents  = src->getPixelComponents();
        //PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) { // Keyer outputs RGBA but may have RGB input
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    } else {
        setPersistentMessage(Message::eMessageError, "", "Failed to fetch source image");
        throwSuiteStatusException(kOfxStatFailed);
    }


    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    auto_ptr<const Image> mask(doMasking ? _maskClip->fetchImage(time) : 0);
    if ( mask.get() ) {
        if ( (mask->getRenderScale().x != args.renderScale.x) ||
             ( mask->getRenderScale().y != args.renderScale.y) ||
             ( ( mask->getField() != eFieldNone) /* for DaVinci Resolve */ && ( mask->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
    }


    // set the images
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
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
    bool clampBlack = _clampBlack->getValueAtTime(time);
    bool clampWhite = _clampWhite->getValueAtTime(time);
    _mix->getValue(mix);


    if ( outputAlpha && (dst->getPixelComponentCount() != 4) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);
    }


    processor.setValues(spillMix, spillExpand, redScale, greenScale, blueScale, brightNess, clampBlack, clampWhite, (float)mix, outputAlpha);

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // DespillPlugin::setupAndProcess

// the overridden render function
void
DespillPlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    // do the rendering
    if (dstComponents == ePixelComponentRGBA) {
        renderForComponents<4>(args);
    } else if (dstComponents == ePixelComponentRGB) {
        renderForComponents<3>(args);
#ifdef OFX_EXTENSIONS_NATRON
    } else if (dstComponents == ePixelComponentXY) {
        renderForComponents<2>(args);
#endif
    }  else {
        assert(dstComponents == ePixelComponentAlpha);
        renderForComponents<1>(args);
    } // switch
} // render

template<int nComponents>
void
DespillPlugin::renderForComponents(const RenderArguments &args)
{
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();

    switch (dstBitDepth) {
    case eBitDepthUByte:
        renderForBitDepth<unsigned char, nComponents, 255>(args);
        break;

    case eBitDepthUShort:
        renderForBitDepth<unsigned short, nComponents, 65535>(args);
        break;

    case eBitDepthFloat:
        renderForBitDepth<float, nComponents, 1>(args);
        break;
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

template <class PIX, int nComponents, int maxValue>
void
DespillPlugin::renderForBitDepth(const RenderArguments &args)
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
DespillPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    bool createAlpha;

    _outputToAlpha->getValue(createAlpha);
    // Set input and output to the same components
    if (createAlpha) {
        clipPreferences.setClipComponents(*_dstClip, ePixelComponentRGBA);
        clipPreferences.setClipComponents(*_srcClip, ePixelComponentRGBA);
    } else {
        PixelComponentEnum srcComps = _srcClip->getPixelComponents();
        clipPreferences.setClipComponents(*_dstClip, srcComps);
    }
}

mDeclarePluginFactory(DespillPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
DespillPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // Say we are a filter context
    desc.addSupportedContext(eContextFilter);
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
DespillPluginFactory::describeInContext(ImageEffectDescriptor &desc,
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
        param->setDisplayRange(0., 1.);
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
        param->setDisplayRange(0., 1.);
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
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamClampBlack);
        param->setLabel(kParamClampBlackLabel);
        param->setHint(kParamClampBlackHint);
        param->setDefault(true);
        param->setAnimates(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 0);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamClampWhite);
        param->setLabel(kParamClampWhiteLabel);
        param->setHint(kParamClampWhiteHint);
        param->setDefault(false);
        param->setAnimates(true);
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
