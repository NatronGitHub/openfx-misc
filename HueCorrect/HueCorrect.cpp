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
 * OFX HueCorrect plugin
 */

#include <cmath>
#include <cfloat> // DBL_MAX
#include <algorithm>
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <windows.h>
#define isnan _isnan
#else
using std::isnan;
#endif

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsCoords.h"
#include "ofxsLut.h"
#include "ofxsMacros.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "HueCorrectOFX"
#define kPluginGrouping "Color"
#define kPluginDescription \
    "See also: http://opticalenquiry.com/nuke/index.php?title=HueCorrect"

#define kPluginIdentifier "net.sf.openfx.HueCorrect"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamHue "hue"
#define kParamHueLabel "Lookup Table"
#define kParamHueHint "Colour lookup table. The master curve is combined with the red, green and blue curves, but not with the alpha curve."


#define kParamLuminanceMath "luminanceMath"
#define kParamLuminanceMathLabel "Luminance Math"
#define kParamLuminanceMathHint "Formula used to compute luminance from RGB values (only used by 'Set Master')."
#define kParamLuminanceMathOptionRec709 "Rec. 709"
#define kParamLuminanceMathOptionRec709Hint "Use Rec. 709 (0.2126r + 0.7152g + 0.0722b)."
#define kParamLuminanceMathOptionRec2020 "Rec. 2020"
#define kParamLuminanceMathOptionRec2020Hint "Use Rec. 2020 (0.2627r + 0.6780g + 0.0593b)."
#define kParamLuminanceMathOptionACESAP0 "ACES AP0"
#define kParamLuminanceMathOptionACESAP0Hint "Use ACES AP0 (0.3439664498r + 0.7281660966g + -0.0721325464b)."
#define kParamLuminanceMathOptionACESAP1 "ACES AP1"
#define kParamLuminanceMathOptionACESAP1Hint "Use ACES AP1 (0.2722287168r +  0.6740817658g +  0.0536895174b)."
#define kParamLuminanceMathOptionCcir601 "CCIR 601"
#define kParamLuminanceMathOptionCcir601Hint "Use CCIR 601 (0.2989r + 0.5866g + 0.1145b)."
#define kParamLuminanceMathOptionAverage "Average"
#define kParamLuminanceMathOptionAverageHint "Use average of r, g, b."
#define kParamLuminanceMathOptionMaximum "Max"
#define kParamLuminanceMathOptionMaximumHint "Use max or r, g, b."

enum LuminanceMathEnum
{
    eLuminanceMathRec709,
    eLuminanceMathRec2020,
    eLuminanceMathACESAP0,
    eLuminanceMathACESAP1,
    eLuminanceMathCcir601,
    eLuminanceMathAverage,
    eLuminanceMathMaximum,
};

#define kParamClampBlack "clampBlack"
#define kParamClampBlackLabel "Clamp Black"
#define kParamClampBlackHint "All colors below 0 on output are set to 0."

#define kParamClampWhite "clampWhite"
#define kParamClampWhiteLabel "Clamp White"
#define kParamClampWhiteHint "All colors above 1 on output are set to 1."

#define kParamMixLuminanceEnable "mixLuminanceEnable"
#define kParamMixLuminanceEnableLabel "Mix Luminance"
#define kParamMixLuminanceEnableHint "Mix luminance"

#define kParamMixLuminance "mixLuminance"
#define kParamMixLuminanceLabel "Mix Luminance"
#define kParamMixLuminanceHint "Mix luminance"

#define kParamPremultChanged "premultChanged"

#define kCurveSat 0
#define kCurveLum 1
#define kCurveRed 2
#define kCurveGreen 3
#define kCurveBlue 4
#define kCurveRSup 5
#define kCurveGSup 6
#define kCurveBSup 7
#define kCurveSatThrsh 8
#define kCurveNb 9

class HueCorrectProcessorBase
    : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImg;
    const OFX::Image *_maskImg;
    bool _doMasking;
    bool _clampBlack;
    bool _clampWhite;
    bool _premult;
    int _premultChannel;
    double _mix;
    bool _maskInvert;

public:
    HueCorrectProcessorBase(OFX::ImageEffect &instance,
                             bool clampBlack,
                             bool clampWhite)
        : OFX::ImageProcessor(instance)
        , _srcImg(0)
        , _maskImg(0)
        , _doMasking(false)
        , _clampBlack(clampBlack)
        , _clampWhite(clampWhite)
        , _premult(false)
        , _premultChannel(3)
        , _mix(1.)
        , _maskInvert(false)
    {
    }

    void setSrcImg(const OFX::Image *v) {_srcImg = v; }

    void setMaskImg(const OFX::Image *v,
                    bool maskInvert) { _maskImg = v; _maskInvert = maskInvert; }

    void doMasking(bool v) {_doMasking = v; }

    void setValues(bool premult,
                   int premultChannel,
                   double mix)
    {
        _premult = premult;
        _premultChannel = premultChannel;
        _mix = mix;
    }

protected:
    // clamp for integer types
    template<class PIX>
    float clamp(float value,
                int maxValue)
    {
        return std::max( 0.f, std::min( value, float(maxValue) ) );
    }

    // clamp for integer types
    template<class PIX>
    double clamp(double value,
                 int maxValue)
    {
        return std::max( 0., std::min( value, double(maxValue) ) );
    }
};


// floats don't clamp
template<>
float
HueCorrectProcessorBase::clamp<float>(float value,
                                       int maxValue)
{
    assert(maxValue == 1.);
    if ( _clampBlack && (value < 0.) ) {
        value = 0.f;
    } else if ( _clampWhite && (value > 1.0) ) {
        value = 1.0f;
    }

    return value;
}

// template to do the processing.
// nbValues is the number of values in the LUT minus 1. For integer types, it should be the same as
// maxValue
template <class PIX, int nComponents, int maxValue, int nbValues>
class HueCorrectProcessor
    : public HueCorrectProcessorBase
{
public:
    // ctor
    HueCorrectProcessor(OFX::ImageEffect &instance,
                         const OFX::RenderArguments &args,
                         OFX::ParametricParam  *hueParam,
                         bool clampBlack,
                         bool clampWhite)
        : HueCorrectProcessorBase(instance, clampBlack, clampWhite)
        , _hueParam(hueParam)
        , _rangeMin(0.)
        , _rangeMax(6.)
    {
        // build the LUT
        assert(_hueParam);
        _time = args.time;
        if (_rangeMin == _rangeMax) {
            // avoid divisions by zero
            _rangeMax = _rangeMin + 1.;
        }
        assert( (PIX)maxValue == maxValue );
        // except for float, maxValue is the same as nbValues
        assert( maxValue == 1 || (maxValue == nbValues) );
        for (int c = 0; c < kCurveNb; ++c) {
            _hue[c].resize(nbValues + 1);
            for (int position = 0; position <= nbValues; ++position) {
                // position to evaluate the param at
                double parametricPos = _rangeMin + (_rangeMax - _rangeMin) * double(position) / nbValues;

                // evaluate the parametric param
                double value = _hueParam->getValue(c, _time, parametricPos);
                // set that in the lut
                _hue[c][position] = (float)clamp<PIX>(value, maxValue);
            }
        }
    }

private:
    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        assert(nComponents == 1 || nComponents == 3 || nComponents == 4);
        assert(_dstImg);
        float tmpPix[nComponents];
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                if ( (nComponents == 1) || (nComponents == 3) ) {
                    // RGB and Alpha: don't premult/unpremult, just apply curves
                    // normalize/denormalize properly
                    for (int c = 0; c < nComponents; ++c) {
                        tmpPix[c] = (float)interpolate(c, srcPix ? (srcPix[c] / (float)maxValue) : 0.f) * maxValue;
                        assert( ( !srcPix || ( !isnan(srcPix[c]) && !isnan(srcPix[c]) ) ) &&
                                !isnan(tmpPix[c]) && !isnan(tmpPix[c]) );
                    }
                    // ofxsMaskMix expects denormalized input
                    ofxsMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                } else {
                    //assert(nComponents == 4);
                    float unpPix[nComponents];
                    ofxsUnPremult<PIX, nComponents, maxValue>(srcPix, unpPix, _premult, _premultChannel);
                    // ofxsUnPremult outputs normalized data
                    for (int c = 0; c < nComponents; ++c) {
                        tmpPix[c] = interpolate(c, unpPix[c]);
                        assert( !isnan(unpPix[c]) && !isnan(unpPix[c]) &&
                                !isnan(tmpPix[c]) && !isnan(tmpPix[c]) );
                    }
                    // ofxsPremultMaskMixPix expects normalized input
                    ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, _premult, _premultChannel, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                }
                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    }

    // on input to interpolate, value should be normalized to the [0-1] range
    float interpolate(int component,
                      float value)
    {
        return 1. - value;
#if 0
        if ( (value < _rangeMin) || (_rangeMax < value) ) {
            // slow version
            int lutIndex = nComponents == 1 ? kCurveAlpha : componentToCurve(component); // special case for components == alpha only
            double ret = _hueParam->getValue(lutIndex, _time, value);
            if ( (nComponents != 1) && (lutIndex != kCurveAlpha) ) {
                ret += _hueParam->getValue(kCurveMaster, _time, value) - value;
            }

            return (float)clamp<PIX>(ret, maxValue);;
        } else {
            float x = (float)(value - _rangeMin) / (float)(_rangeMax - _rangeMin);
            int i = (int)(x * nbValues);
            assert(0 <= i && i <= nbValues);
            float alpha = std::max( 0.f, std::min(x * nbValues - i, 1.f) );
            float a = _hue[component][i];
            float b = (i  < nbValues) ? _hue[component][i + 1] : 0.f;

            return a * (1.f - alpha) + b * alpha;
        }
#endif
    }

private:
    std::vector<float> _hue[kCurveNb];
    OFX::ParametricParam*  _hueParam;
    double _time;
    double _rangeMin;
    double _rangeMax;
};

static
double luminance(double r,
                 double g,
                 double b,
                 LuminanceMathEnum luminanceMath)
{
    switch (luminanceMath) {
        case eLuminanceMathRec709:
        default:
            return Color::rgb709_to_y(r, g, b);

        case eLuminanceMathRec2020: // https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2087-0-201510-I!!PDF-E.pdf

            return Color::rgb2020_to_y(r, g, b);
        case eLuminanceMathACESAP0: // https://en.wikipedia.org/wiki/Academy_Color_Encoding_System#Converting_ACES_RGB_values_to_CIE_XYZ_values

            return Color::rgbACESAP0_to_y(r, g, b);
        case eLuminanceMathACESAP1: // https://en.wikipedia.org/wiki/Academy_Color_Encoding_System#Converting_ACES_RGB_values_to_CIE_XYZ_values

            return Color::rgbACESAP1_to_y(r, g, b);
        case eLuminanceMathCcir601:

            return 0.2989 * r + 0.5866 * g + 0.1145 * b;
        case eLuminanceMathAverage:

            return (r + g + b) / 3;
        case eLuminanceMathMaximum:
            
            return std::max(std::max(r, g), b);
    }
}

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class HueCorrectPlugin
    : public OFX::ImageEffect
{
public:
    HueCorrectPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(0)
        , _srcClip(0)
        , _maskClip(0)
        , _luminanceMath(0)
        , _premultChanged(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentAlpha ||
                             _dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == OFX::eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentAlpha ||
                               _srcClip->getPixelComponents() == ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA) ) );

        _maskClip = fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || !_maskClip->isConnected() || _maskClip->getPixelComponents() == ePixelComponentAlpha);
        _hue = fetchParametricParam(kParamHue);
        _luminanceMath = fetchChoiceParam(kParamLuminanceMath);
        assert(_luminanceMath);
        _clampBlack = fetchBooleanParam(kParamClampBlack);
        _clampWhite = fetchBooleanParam(kParamClampWhite);
        assert(_clampBlack && _clampWhite);
        _premult = fetchBooleanParam(kParamPremult);
        _premultChannel = fetchChoiceParam(kParamPremultChannel);
        assert(_premult && _premultChannel);
        _mix = fetchDoubleParam(kParamMix);
        _maskApply = paramExists(kParamMaskApply) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
        _premultChanged = fetchBooleanParam(kParamPremultChanged);
        assert(_premultChanged);
    }

private:
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;
    virtual bool isIdentity(const OFX::IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    template <int nComponents>
    void renderForComponents(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth);

    void setupAndProcess(HueCorrectProcessorBase &, const OFX::RenderArguments &args);

    virtual void changedParam(const OFX::InstanceChangedArgs &args,
                              const std::string &paramName) OVERRIDE FINAL
    {
        const double time = args.time;
        if ( (paramName == kParamPremult) && (args.reason == OFX::eChangeUserEdit) ) {
            _premultChanged->setValue(true);
        }
    } // changedParam

private:
    Clip *_dstClip;
    Clip *_srcClip;
    Clip *_maskClip;
    ParametricParam  *_hue;
    ChoiceParam* _luminanceMath;
    BooleanParam* _clampBlack;
    BooleanParam* _clampWhite;
    BooleanParam* _premult;
    ChoiceParam* _premultChannel;
    DoubleParam* _mix;
    BooleanParam* _maskApply;
    BooleanParam* _maskInvert;
    BooleanParam* _premultChanged; // set to true the first time the user connects src
};


void
HueCorrectPlugin::setupAndProcess(HueCorrectProcessorBase &processor,
                                   const OFX::RenderArguments &args)
{
    const double time = args.time;
    assert(_dstClip);
    std::auto_ptr<OFX::Image> dst( _dstClip->fetchImage(time) );
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
    std::auto_ptr<const OFX::Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                         _srcClip->fetchImage(time) : 0 );
    if ( src.get() ) {
        if ( (src->getRenderScale().x != args.renderScale.x) ||
             ( src->getRenderScale().y != args.renderScale.y) ||
             ( ( src->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( src->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    std::auto_ptr<const OFX::Image> mask(doMasking ? _maskClip->fetchImage(time) : 0);
    if (doMasking) {
        if ( mask.get() ) {
            if ( (mask->getRenderScale().x != args.renderScale.x) ||
                 ( mask->getRenderScale().y != args.renderScale.y) ||
                 ( ( mask->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( mask->getField() != args.fieldToRender) ) ) {
                setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                OFX::throwSuiteStatusException(kOfxStatFailed);
            }
        }
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    if ( src.get() && dst.get() ) {
        OFX::BitDepthEnum srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
        OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();

        // see if they have the same depths and bytes and all
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );
    processor.setRenderWindow(args.renderWindow);
    bool premult;
    int premultChannel;
    _premult->getValueAtTime(time, premult);
    _premultChannel->getValueAtTime(time, premultChannel);
    double mix;
    _mix->getValueAtTime(time, mix);
    processor.setValues(premult, premultChannel, mix);
    processor.process();
} // HueCorrectPlugin::setupAndProcess

// the internal render function
template <int nComponents>
void
HueCorrectPlugin::renderForComponents(const OFX::RenderArguments &args,
                                       OFX::BitDepthEnum dstBitDepth)
{
    const double time = args.time;


    bool clampBlack = _clampBlack->getValueAtTime(time);
    bool clampWhite = _clampWhite->getValueAtTime(time);
    switch (dstBitDepth) {
    case OFX::eBitDepthUByte: {
        HueCorrectProcessor<unsigned char, nComponents, 255, 255> fred(*this, args, _hue, clampBlack, clampWhite);
        setupAndProcess(fred, args);
        break;
    }
    case OFX::eBitDepthUShort: {
        HueCorrectProcessor<unsigned short, nComponents, 65535, 65535> fred(*this, args, _hue, clampBlack, clampWhite);
        setupAndProcess(fred, args);
        break;
    }
    case OFX::eBitDepthFloat: {
        HueCorrectProcessor<float, nComponents, 1, 1023> fred(*this, args, _hue, clampBlack, clampWhite);
        setupAndProcess(fred, args);
        break;
    }
    default:
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

void
HueCorrectPlugin::render(const OFX::RenderArguments &args)
{
    OFX::BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    if (dstComponents == OFX::ePixelComponentRGBA) {
        renderForComponents<4>(args, dstBitDepth);
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        renderForComponents<3>(args, dstBitDepth);
    } else if (dstComponents == OFX::ePixelComponentXY) {
        renderForComponents<2>(args, dstBitDepth);
    } else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        renderForComponents<1>(args, dstBitDepth);
    }
}

bool
HueCorrectPlugin::isIdentity(const IsIdentityArguments &args,
                              Clip * &identityClip,
                              double & /*identityTime*/)
{
    const double time = args.time;
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );

    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        if (!maskInvert) {
            OfxRectI maskRoD;
            if (OFX::getImageEffectHostDescription()->supportsMultiResolution) {
                // In Sony Catalyst Edit, clipGetRegionOfDefinition returns the RoD in pixels instead of canonical coordinates.
                // In hosts that do not support multiResolution (e.g. Sony Catalyst Edit), all inputs have the same RoD anyway.
                OFX::Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
                // effect is identity if the renderWindow doesn't intersect the mask RoD
                if ( !OFX::Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0) ) {
                    identityClip = _srcClip;

                    return true;
                }
            }
        }
    }

    return false;
}

void
HueCorrectPlugin::changedClip(const InstanceChangedArgs &args,
                               const std::string &clipName)
{
    if ( (clipName == kOfxImageEffectSimpleSourceClipName) &&
         _srcClip && _srcClip->isConnected() &&
         !_premultChanged->getValue() &&
         ( args.reason == OFX::eChangeUserEdit) ) {
        if (_srcClip->getPixelComponents() != ePixelComponentRGBA) {
            _premult->setValue(false);
        } else {
            switch ( _srcClip->getPreMultiplication() ) {
            case eImageOpaque:
                _premult->setValue(false);
                break;
            case eImagePreMultiplied:
                _premult->setValue(true);
                break;
            case eImageUnPreMultiplied:
                _premult->setValue(false);
                break;
            }
        }
    }
}

class HueCorrectInteract
    : public OFX::ParamInteract
{
public:
    HueCorrectInteract(OfxInteractHandle handle,
                        OFX::ImageEffect* effect,
                        const std::string& /*paramName*/) :
        OFX::ParamInteract(handle, effect)
    {
        //_hueParam = effect->fetchParametricParam(paramName);
        //setColourPicking(false); // no color picking
    }

    virtual bool draw(const OFX::DrawArgs &args) OVERRIDE FINAL
    {
        const double time = args.time;

        return false;
#if 0
        // let us draw one slice every 8 pixels
        const int sliceWidth = 8;
        int nbValues = args.pixelScale.x > 0 ? std::ceil( (rangeMax - rangeMin) / (sliceWidth * args.pixelScale.x) ) : 1;
        const int nComponents = 3;
        GLfloat color[nComponents];

        if ( _showRamp->getValueAtTime(time) ) {
            glBegin (GL_TRIANGLE_STRIP);

            for (int position = 0; position <= nbValues; ++position) {
                // position to evaluate the param at
                double parametricPos = rangeMin + (rangeMax - rangeMin) * double(position) / nbValues;

                for (int component = 0; component < nComponents; ++component) {
                    int lutIndex = componentToCurve(component); // special case for components == alpha only
                    // evaluate the parametric param
                    double value = _hueParam->getValue(lutIndex, time, parametricPos);
                    value += _hueParam->getValue(kCurveMaster, time, parametricPos) - parametricPos;
                    // set that in the lut
                    color[component] = value;
                }
                glColor3f(color[0], color[1], color[2]);
                glVertex2f(parametricPos, rangeMin);
                glVertex2f(parametricPos, rangeMax);
            }
            
            glEnd();
        }

        if (args.hasPickerColour) {
            glLineWidth(1.5);
            glBegin(GL_LINES);
            {
                // the following are magic colors, they all have the same Rec709 luminance
                const OfxRGBColourD red   = {0.711519527404004, 0.164533420851110, 0.164533420851110};      //set red color to red curve
                const OfxRGBColourD green = {0., 0.546986106552894, 0.};        //set green color to green curve
                const OfxRGBColourD blue  = {0.288480472595996, 0.288480472595996, 0.835466579148890};      //set blue color to blue curve
                const OfxRGBColourD alpha  = {0.398979, 0.398979, 0.398979};
                glColor3f(red.r, red.g, red.b);
                glVertex2f(args.pickerColour.r, rangeMin);
                glVertex2f(args.pickerColour.r, rangeMax);
                glColor3f(green.r, green.g, green.b);
                glVertex2f(args.pickerColour.g, rangeMin);
                glVertex2f(args.pickerColour.g, rangeMax);
                glColor3f(blue.r, blue.g, blue.b);
                glVertex2f(args.pickerColour.b, rangeMin);
                glVertex2f(args.pickerColour.b, rangeMax);
                glColor3f(alpha.r, alpha.g, alpha.b);
                glVertex2f(args.pickerColour.a, rangeMin);
                glVertex2f(args.pickerColour.a, rangeMax);
            }
            glEnd();
        }
        return true;
#endif
    }

    virtual ~HueCorrectInteract() {}

protected:
    //OFX::ParametricParam* _hueParam;
};

// We are lucky, there's only one hue param, so we need only one interact
// descriptor. If there were several, be would have to use a template parameter,
// as in propTester.cpp
class HueCorrectInteractDescriptor
    : public OFX::DefaultParamInteractDescriptor<HueCorrectInteractDescriptor, HueCorrectInteract>
{
public:
    virtual void describe() OVERRIDE FINAL
    {
        setColourPicking(true);
    }
};

mDeclarePluginFactory(HueCorrectPluginFactory, {}, {});

void
HueCorrectPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextPaint);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
    // returning an error here crashes Nuke
    //if (!OFX::getImageEffectHostDescription()->supportsParametricParameter) {
    //  throwHostMissingSuiteException(kOfxParametricParameterSuite);
    //}
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentRGBA);
#endif
}

void
HueCorrectPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                            OFX::ContextEnum context)
{
    const ImageEffectHostDescription &gHostDescription = *OFX::getImageEffectHostDescription();
    const bool supportsParametricParameter = ( gHostDescription.supportsParametricParameter &&
                                               !(gHostDescription.hostName == "uk.co.thefoundry.nuke" &&
                                                 8 <= gHostDescription.versionMajor && gHostDescription.versionMajor <= 10) );  // Nuke 8-10 are known to *not* support Parametric

    if (!supportsParametricParameter) {
        throwHostMissingSuiteException(kOfxParametricParameterSuite);
    }

    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    assert(srcClip);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentXY);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    assert(dstClip);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentXY);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    ClipDescriptor *maskClip = (context == eContextPaint) ? desc.defineClip("Brush") : desc.defineClip("Mask");
    maskClip->addSupportedComponent(ePixelComponentAlpha);
    maskClip->setTemporalClipAccess(false);
    if (context != eContextPaint) {
        maskClip->setOptional(true);
    }
    maskClip->setSupportsTiles(kSupportsTiles);
    maskClip->setIsMask(true);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // define it
    {
        OFX::ParametricParamDescriptor* param = desc.defineParametricParam(kParamHue);
        assert(param);
        param->setLabel(kParamHueLabel);
        param->setHint(kParamHueHint);
        {
            HueCorrectInteractDescriptor* interact = new HueCorrectInteractDescriptor;
            param->setInteractDescriptor(interact);
        }

       // define it as three dimensional
        param->setDimension(kCurveNb);

        // label our dimensions are r/g/b
        param->setDimensionLabel("sat", kCurveSat);
        param->setDimensionLabel("lum", kCurveLum);
        param->setDimensionLabel("red", kCurveRed);
        param->setDimensionLabel("green", kCurveGreen);
        param->setDimensionLabel("blue", kCurveBlue);
        param->setDimensionLabel("r_sup", kCurveRSup);
        param->setDimensionLabel("g_sup", kCurveGSup);
        param->setDimensionLabel("b_sup", kCurveBSup);
        param->setDimensionLabel("sat_thrsh", kCurveSatThrsh);

        // set the UI colour for each dimension
        //const OfxRGBColourD master  = {0.9, 0.9, 0.9};
        // the following are magic colors, they all have the same Rec709 luminance
        const OfxRGBColourD red   = {0.711519527404004, 0.164533420851110, 0.164533420851110};      //set red color to red curve
        const OfxRGBColourD green = {0., 0.546986106552894, 0.};        //set green color to green curve
        const OfxRGBColourD blue  = {0.288480472595996, 0.288480472595996, 0.835466579148890};      //set blue color to blue curve
        //const OfxRGBColourD alpha  = {0.398979, 0.398979, 0.398979};
        //param->setUIColour( kCurveSat, sat );
        //param->setUIColour( kCurveLum, lum );
        param->setUIColour( kCurveRed, red );
        param->setUIColour( kCurveGreen, green );
        param->setUIColour( kCurveBlue, blue );
        param->setUIColour( kCurveRSup, red );
        param->setUIColour( kCurveGSup, green );
        param->setUIColour( kCurveBSup, blue );
        //param->setUIColour( kCurveSatThrsh, alpha );

        // set the min/max parametric range to 0..6
        param->setRange(0.0, 6.0);

        // minimum/maximum: are these supported by OpenFX?
        //param->setRange(0., 2.);
        //param->setDisplayRange(0., 2.);

        // set a default curve
        for (int c = 0; c < kCurveNb; ++c) {
            for (int p = 0; p <=6; ++p) {
                // add a control point at p
                param->addControlPoint(c, // curve to set
                                       0.0,   // time, ignored in this case, as we are not adding a key
                                       p,   // parametric position, zero
                                       (c == kCurveSatThrsh) ? 0. : 1.,   // value to be
                                       false);   // don't add a key
            }
        }

        if (page) {
            page->addChild(*param);
        }
    }
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamLuminanceMath);
        param->setLabel(kParamLuminanceMathLabel);
        param->setHint(kParamLuminanceMathHint);
        param->setEvaluateOnChange(false); // WARNING: RENDER IS NOT AFFECTED BY THIS OPTION IN THIS PLUGIN
        assert(param->getNOptions() == eLuminanceMathRec709);
        param->appendOption(kParamLuminanceMathOptionRec709, kParamLuminanceMathOptionRec709Hint);
        assert(param->getNOptions() == eLuminanceMathRec2020);
        param->appendOption(kParamLuminanceMathOptionRec2020, kParamLuminanceMathOptionRec2020Hint);
        assert(param->getNOptions() == eLuminanceMathACESAP0);
        param->appendOption(kParamLuminanceMathOptionACESAP0, kParamLuminanceMathOptionACESAP0Hint);
        assert(param->getNOptions() == eLuminanceMathACESAP1);
        param->appendOption(kParamLuminanceMathOptionACESAP1, kParamLuminanceMathOptionACESAP1Hint);
        assert(param->getNOptions() == eLuminanceMathCcir601);
        param->appendOption(kParamLuminanceMathOptionCcir601, kParamLuminanceMathOptionCcir601Hint);
        assert(param->getNOptions() == eLuminanceMathAverage);
        param->appendOption(kParamLuminanceMathOptionAverage, kParamLuminanceMathOptionAverageHint);
        assert(param->getNOptions() == eLuminanceMathMaximum);
        param->appendOption(kParamLuminanceMathOptionMaximum, kParamLuminanceMathOptionMaximumHint);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamClampBlack);
        param->setLabel(kParamClampBlackLabel);
        param->setHint(kParamClampBlackHint);
        param->setDefault(false);
        param->setAnimates(true);
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

    ofxsPremultDescribeParams(desc, page);
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamMixLuminanceEnable);
        param->setLabel(kParamMixLuminanceEnableLabel);
        param->setHint(kParamMixLuminanceEnableHint);
        param->setDefault(true);
        param->setAnimates(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamMixLuminance);
        param->setLabel(kParamMixLuminanceLabel);
        param->setHint(kParamMixLuminanceHint);
        param->setDefault(0.);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    ofxsMaskMixDescribeParams(desc, page);

    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamPremultChanged);
        param->setDefault(false);
        param->setIsSecretAndDisabled(true);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }
} // HueCorrectPluginFactory::describeInContext

OFX::ImageEffect*
HueCorrectPluginFactory::createInstance(OfxImageEffectHandle handle,
                                         OFX::ContextEnum /*context*/)
{
    return new HueCorrectPlugin(handle);
}

static HueCorrectPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
