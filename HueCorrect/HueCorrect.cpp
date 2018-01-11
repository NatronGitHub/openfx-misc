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
 * OFX HueCorrect and HueKeyer plugins
 */

#include <cmath>
#include <cfloat> // DBL_MAX
#include <algorithm>

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

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
    "Apply hue-dependent color adjustments using lookup curves.\n" \
    "Hue and saturation are computed from the the source RGB values. Depending on the hue value, the various adjustment values are computed, and then applied:\n" \
    "sat: saturation gain. This modification is applied last.\n" \
    "lum: luminance gain\n" \
    "red: red gain\n" \
    "green: green gain\n" \
    "blue: blue gain\n" \
    "r_sup: red suppression. If r > min(g,b),  r = min(g,b) + r_sup * (r-min(g,b))\n" \
    "g_sup: green suppression\n" \
    "b_sup: blue suppression\n" \
    "sat_thrsh: if source saturation is below this value, do not apply the lum, red, green, blue gains. Above this value, apply gain progressively.\n" \
    "\n" \
    "The 'Luminance Mix' parameter may be used to restore partially or fully the original luminance (luminance is computed using the 'Luminance Math' parameter).\n" \
    "\n" \
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
#define kParamHueLabel "Hue Curves"
#define kParamHueHint "Hue-dependent adjustment lookup curves:\n" \
    "sat: saturation gain. This modification is applied last.\n" \
    "lum: luminance gain\n" \
    "red: red gain\n" \
    "green: green gain\n" \
    "blue: blue gain\n" \
    "r_sup: red suppression. If r > min(g,b),  r = min(g,b) + r_sup * (r-min(g,b))\n" \
    "g_sup: green suppression\n" \
    "b_sup: blue suppression\n" \
    "sat_thrsh: if source saturation is below this value, do not apply the lum, red, green, blue gains. Above this value, apply gain progressively."


#define kParamLuminanceMath "luminanceMath"
#define kParamLuminanceMathLabel "Luminance Math"
#define kParamLuminanceMathHint "Formula used to compute luminance from RGB values (only used by 'Set Master')."
#define kParamLuminanceMathOptionRec709 "Rec. 709", "Use Rec. 709 (0.2126r + 0.7152g + 0.0722b).", "rec709"
#define kParamLuminanceMathOptionRec2020 "Rec. 2020", "Use Rec. 2020 (0.2627r + 0.6780g + 0.0593b).", "rec2020"
#define kParamLuminanceMathOptionACESAP0 "ACES AP0", "Use ACES AP0 (0.3439664498r + 0.7281660966g + -0.0721325464b).", "acesap0"
#define kParamLuminanceMathOptionACESAP1 "ACES AP1", "Use ACES AP1 (0.2722287168r +  0.6740817658g +  0.0536895174b).", "acesap1"
#define kParamLuminanceMathOptionCcir601 "CCIR 601", "Use CCIR 601 (0.2989r + 0.5866g + 0.1145b).", "ccir601"
#define kParamLuminanceMathOptionAverage "Average", "Use average of r, g, b.", "average"
#define kParamLuminanceMathOptionMaximum "Max", "Use max or r, g, b.", "max"

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
#define kParamMixLuminanceLabel ""
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
    : public ImageProcessor
{
protected:
    const Image *_srcImg;
    const Image *_maskImg;
    bool _doMasking;
    LuminanceMathEnum _luminanceMath;
    double _luminanceMix;
    const bool _clampBlack;
    const bool _clampWhite;
    bool _premult;
    int _premultChannel;
    double _mix;
    bool _maskInvert;

public:
    HueCorrectProcessorBase(ImageEffect &instance,
                            bool clampBlack,
                            bool clampWhite)
        : ImageProcessor(instance)
        , _srcImg(NULL)
        , _maskImg(NULL)
        , _doMasking(false)
        , _luminanceMath(eLuminanceMathRec709)
        , _luminanceMix(0.)
        , _clampBlack(clampBlack)
        , _clampWhite(clampWhite)
        , _premult(false)
        , _premultChannel(3)
        , _mix(1.)
        , _maskInvert(false)
    {
    }

    void setSrcImg(const Image *v) {_srcImg = v; }

    void setMaskImg(const Image *v,
                    bool maskInvert) { _maskImg = v; _maskInvert = maskInvert; }

    void doMasking(bool v) {_doMasking = v; }

    void setValues(LuminanceMathEnum luminanceMath,
                   double luminanceMix,
                   bool premult,
                   int premultChannel,
                   double mix)
    {
        _luminanceMath = luminanceMath;
        _luminanceMix = luminanceMix;
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


// floats don't clamp except if _clampBlack or _clampWhite
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

static
double
luminance(double r,
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

// template to do the processing.
// nbValues is the number of values in the LUT minus 1. For integer types, it should be the same as
// maxValue
template <class PIX, int nComponents, int maxValue, int nbValues>
class HueCorrectProcessor
    : public HueCorrectProcessorBase
{
public:
    // ctor
    HueCorrectProcessor(ImageEffect &instance,
                        const RenderArguments &args,
                        ParametricParam  *hueParam,
                        bool clampBlack,
                        bool clampWhite)
        : HueCorrectProcessorBase(instance, clampBlack, clampWhite)
        , _hueParam(hueParam)
    {
        // build the LUT
        assert(_hueParam);
        _time = args.time;
        for (int c = 0; c < kCurveNb; ++c) {
            _hue[c].resize(nbValues + 1);
            for (int position = 0; position <= nbValues; ++position) {
                // position to evaluate the param at
                double parametricPos = 6 * double(position) / nbValues;

                // evaluate the parametric param
                double value = _hueParam->getValue(c, _time, parametricPos);

                // all the values (in HueCorrect) must be positive. We don't care if sat_thrsh goes above 1.
                value = std::max(0., value);
                // set that in the lut
                _hue[c][position] = value;
            }
        }
    }

private:
    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        assert(nComponents == 3 || nComponents == 4);
        assert(_dstImg);
        float tmpPix[4];
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                float unpPix[4];
                ofxsUnPremult<PIX, nComponents, maxValue>(srcPix, unpPix, _premult, _premultChannel);
                // ofxsUnPremult outputs normalized data

                float r = unpPix[0];
                float g = unpPix[1];
                float b = unpPix[2];
                float l_in = 0.;
                if (_luminanceMix > 0.) {
                    l_in = luminance(r, g, b, _luminanceMath);
                }
                float h, s, v;
                Color::rgb_to_hsv( r, g, b, &h, &s, &v );
                h = h * 6 + 1;
                if (h > 6) {
                    h -= 6;
                }
                double sat = interpolate(kCurveSat, h);
                double lum = interpolate(kCurveLum, h);
                double red = interpolate(kCurveRed, h);
                double green = interpolate(kCurveGreen, h);
                double blue = interpolate(kCurveBlue, h);
                double r_sup = interpolate(kCurveRSup, h);
                double g_sup = interpolate(kCurveGSup, h);
                double b_sup = interpolate(kCurveBSup, h);
                float sat_thrsh = interpolate(kCurveSatThrsh, h);

                if (r_sup != 1.) {
                    // If r > min(g,b),  r = min(g,b) + r_sup * (r-min(g,b))
                    float m = std::min(g, b);
                    if (r > m) {
                        r = m + r_sup * (r - m);
                    }
                }
                if (g_sup != 1.) {
                    float m = std::min(r, b);
                    if (g > m) {
                        g = m + g_sup * (g - m);
                    }
                }
                if (b_sup != 1.) {
                    float m = std::min(r, g);
                    if (b > m) {
                        b = m + b_sup * (b - m);
                    }
                }
                if (s > sat_thrsh) {
                    // Get a smooth effect: identity at s=sat_thrsh, full if sat_thrsh = 0
                    r *= (sat_thrsh * 1. + (s - sat_thrsh) * red * lum) / s; // red * lum
                    g *= (sat_thrsh * 1. + (s - sat_thrsh) * green * lum) / s; // green * lum;
                    b *= (sat_thrsh * 1. + (s - sat_thrsh) * blue * lum) / s; // blue * lum;
                } else if (sat_thrsh == 0.) {
                    assert(s == 0.);
                    r *= red * lum; // red * lum
                    g *= green * lum; // green * lum;
                    b *= blue * lum; // blue * lum;
                }
                if (sat != 1.) {
                    float l_sat = luminance(r, g, b, _luminanceMath);
                    r = (1. - sat) * l_sat + sat * r;
                    g = (1. - sat) * l_sat + sat * g;
                    b = (1. - sat) * l_sat + sat * b;
                }
                if (_luminanceMix > 0.) {
                    float l_out = luminance(r, g, b, _luminanceMath);
                    if (l_out <= 0.) {
                        r = g = b = l_in;
                    } else {
                        double f = 1 + _luminanceMix * (l_in / l_out - 1.);
                        r *= f;
                        g *= f;
                        b *= f;
                    }
                }

                tmpPix[0] = clamp<float>(r, 1.);
                tmpPix[1] = clamp<float>(g, 1.);
                tmpPix[2] = clamp<float>(b, 1.);
                tmpPix[3] = unpPix[3]; // alpha is left unchanged
                for (int c = 0; c < nComponents; ++c) {
                    assert( !OFX::IsNaN(unpPix[c]) && !OFX::IsNaN(unpPix[c]) &&
                            !OFX::IsNaN(tmpPix[c]) && !OFX::IsNaN(tmpPix[c]) );
                }

                // ofxsPremultMaskMixPix expects normalized input
                ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, _premult, _premultChannel, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    } // multiThreadProcessImages

    double interpolate(int c, // the curve number
                       double value)
    {
        if ( (value < 0.) || (6. < value) ) {
            // slow version
            double ret = _hueParam->getValue(c, _time, value);

            return ret;
        } else {
            double x = value / 6.;
            int i = (int)(x * nbValues);
            assert(0 <= i && i <= nbValues);
            double alpha = std::max( 0., std::min(x * nbValues - i, 1.) );
            double a = _hue[c][i];
            double b = (i  < nbValues) ? _hue[c][i + 1] : 0.f;

            return a * (1.f - alpha) + b * alpha;
        }
    }

private:
    std::vector<double> _hue[kCurveNb];
    ParametricParam*  _hueParam;
    double _time;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class HueCorrectPlugin
    : public ImageEffect
{
public:
    HueCorrectPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _maskClip(NULL)
        , _luminanceMath(NULL)
        , _premultChanged(NULL)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentAlpha ||
                             _dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentAlpha ||
                               _srcClip->getPixelComponents() == ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA) ) );

        _maskClip = fetchClip(getContext() == eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || !_maskClip->isConnected() || _maskClip->getPixelComponents() == ePixelComponentAlpha);
        _hue = fetchParametricParam(kParamHue);
        _luminanceMath = fetchChoiceParam(kParamLuminanceMath);
        _luminanceMixEnable = fetchBooleanParam(kParamMixLuminanceEnable);
        _luminanceMix = fetchDoubleParam(kParamMixLuminance);
        assert(_luminanceMath);
        _clampBlack = fetchBooleanParam(kParamClampBlack);
        _clampWhite = fetchBooleanParam(kParamClampWhite);
        assert(_clampBlack && _clampWhite);
        _premult = fetchBooleanParam(kParamPremult);
        _premultChannel = fetchChoiceParam(kParamPremultChannel);
        assert(_premult && _premultChannel);
        _mix = fetchDoubleParam(kParamMix);
        _maskApply = ( ofxsMaskIsAlwaysConnected( OFX::getImageEffectHostDescription() ) && paramExists(kParamMaskApply) ) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
        _premultChanged = fetchBooleanParam(kParamPremultChanged);
        assert(_premultChanged);
        _luminanceMix->setEnabled( _luminanceMixEnable->getValue() );
    }

private:
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    template <int nComponents>
    void renderForComponents(const RenderArguments &args, BitDepthEnum dstBitDepth);

    void setupAndProcess(HueCorrectProcessorBase &, const RenderArguments &args);

    virtual void changedParam(const InstanceChangedArgs &args,
                              const std::string &paramName) OVERRIDE FINAL
    {
        const double time = args.time;

        if ( (paramName == kParamPremult) && (args.reason == eChangeUserEdit) ) {
            _premultChanged->setValue(true);
        }
        if ( (paramName == kParamMixLuminanceEnable) && (args.reason == eChangeUserEdit) ) {
            _luminanceMix->setEnabled( _luminanceMixEnable->getValueAtTime(time) );
        }
    } // changedParam

private:
    Clip *_dstClip;
    Clip *_srcClip;
    Clip *_maskClip;
    ParametricParam  *_hue;
    ChoiceParam* _luminanceMath;
    BooleanParam* _luminanceMixEnable;
    DoubleParam* _luminanceMix;
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
                                  const RenderArguments &args)
{
    const double time = args.time;

    assert(_dstClip);
    auto_ptr<Image> dst( _dstClip->fetchImage(time) );
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
        PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    auto_ptr<const Image> mask(doMasking ? _maskClip->fetchImage(time) : 0);
    if (doMasking) {
        if ( mask.get() ) {
            if ( (mask->getRenderScale().x != args.renderScale.x) ||
                 ( mask->getRenderScale().y != args.renderScale.y) ||
                 ( ( mask->getField() != eFieldNone) /* for DaVinci Resolve */ && ( mask->getField() != args.fieldToRender) ) ) {
                setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                throwSuiteStatusException(kOfxStatFailed);
            }
        }
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    if ( src.get() && dst.get() ) {
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();
        BitDepthEnum dstBitDepth       = dst->getPixelDepth();
        PixelComponentEnum dstComponents  = dst->getPixelComponents();

        // see if they have the same depths and bytes and all
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );
    processor.setRenderWindow(args.renderWindow);
    LuminanceMathEnum luminanceMath = (LuminanceMathEnum)_luminanceMath->getValueAtTime(time);
    bool luminanceMixEnable = _luminanceMixEnable->getValueAtTime(time);
    double luminanceMix = luminanceMixEnable ? _luminanceMix->getValueAtTime(time) : 0;
    bool premult = _premult->getValueAtTime(time);
    int premultChannel = _premultChannel->getValueAtTime(time);
    double mix = _mix->getValueAtTime(time);
    processor.setValues(luminanceMath, luminanceMix, premult, premultChannel, mix);
    processor.process();
} // HueCorrectPlugin::setupAndProcess

// the internal render function
template <int nComponents>
void
HueCorrectPlugin::renderForComponents(const RenderArguments &args,
                                      BitDepthEnum dstBitDepth)
{
    const double time = args.time;
    bool clampBlack = _clampBlack->getValueAtTime(time);
    bool clampWhite = _clampWhite->getValueAtTime(time);

    switch (dstBitDepth) {
    case eBitDepthUByte: {
        HueCorrectProcessor<unsigned char, nComponents, 255, 255> fred(*this, args, _hue, clampBlack, clampWhite);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthUShort: {
        HueCorrectProcessor<unsigned short, nComponents, 65535, 65535> fred(*this, args, _hue, clampBlack, clampWhite);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthFloat: {
        HueCorrectProcessor<float, nComponents, 1, 1023> fred(*this, args, _hue, clampBlack, clampWhite);
        setupAndProcess(fred, args);
        break;
    }
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

void
HueCorrectPlugin::render(const RenderArguments &args)
{
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    if (dstComponents == ePixelComponentRGBA) {
        renderForComponents<4>(args, dstBitDepth);
    } else if (dstComponents == ePixelComponentRGB) {
        renderForComponents<3>(args, dstBitDepth);
        //} else if (dstComponents == ePixelComponentXY) {
        //    renderForComponents<2>(args, dstBitDepth);
        //} else {
        //    assert(dstComponents == ePixelComponentAlpha);
        //    renderForComponents<1>(args, dstBitDepth);
    } else {
        throwSuiteStatusException(kOfxStatErrFormat);
    }
}

bool
HueCorrectPlugin::isIdentity(const IsIdentityArguments &args,
                             Clip * &identityClip,
                             double & /*identityTime*/
                             , int& /*view*/, std::string& /*plane*/)
{
    const double time = args.time;
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );

    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        if (!maskInvert) {
            OfxRectI maskRoD;
            if (getImageEffectHostDescription()->supportsMultiResolution) {
                // In Sony Catalyst Edit, clipGetRegionOfDefinition returns the RoD in pixels instead of canonical coordinates.
                // In hosts that do not support multiResolution (e.g. Sony Catalyst Edit), all inputs have the same RoD anyway.
                Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
                // effect is identity if the renderWindow doesn't intersect the mask RoD
                if ( !Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0) ) {
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
         ( args.reason == eChangeUserEdit) ) {
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
    : public ParamInteract
{
public:
    HueCorrectInteract(OfxInteractHandle handle,
                       ImageEffect* effect,
                       const std::string& paramName)
        : ParamInteract(handle, effect)
        , _hueParam(NULL)
        , _xMin(0.)
        , _xMax(0.)
        , _yMin(0.)
        , _yMax(0.)
    {
        _hueParam = effect->fetchParametricParam(paramName);
        setColourPicking(true);
        _hueParam->getRange(_xMin, _xMax);
        _hueParam->getDimensionDisplayRange(0, _yMin, _yMax);
        if ( (_yMin == 0.) && (_yMax == 0.) ) {
            _yMax = 2.; // default for hosts that don't support displayrange
        }
    }

    virtual bool draw(const DrawArgs &args) OVERRIDE FINAL
    {
        //const double time = args.time;

        // let us draw one slice every 8 pixels
        const int sliceWidth = 8;
        const float s = 1.;
        const float v = 0.3;
        int nbValues = args.pixelScale.x > 0 ? std::ceil( (_xMax - _xMin) / (sliceWidth * args.pixelScale.x) ) : 1;

        glBegin (GL_TRIANGLE_STRIP);

        for (int position = 0; position <= nbValues; ++position) {
            // position to evaluate the param at
            double parametricPos = _xMin + (_xMax - _xMin) * double(position) / nbValues;

            // red is at parametricPos = 1
            float h = (parametricPos - 1) / 6;
            float r, g, b;
            Color::hsv_to_rgb( h, s, v, &r, &g, &b );
            glColor3f(r, g, b);
            glVertex2f(parametricPos, _yMin);
            glVertex2f(parametricPos, _yMax);
        }

        glEnd();

        if (args.hasPickerColour) {
            glLineWidth(1.5);
            glBegin(GL_LINES);
            {
                float h, s, v;
                Color::rgb_to_hsv( args.pickerColour.r, args.pickerColour.g, args.pickerColour.b, &h, &s, &v );
                const OfxRGBColourD yellow   = {1, 1, 0};
                const OfxRGBColourD grey  = {2. / 3., 2. / 3., 2. / 3.};
                glColor3f(yellow.r, yellow.g, yellow.b);
                // map [0,1] to [0,6]
                h = _xMin + h * (_xMax - _xMin) + 1;
                if (h > 6) {
                    h -= 6;
                }
                glVertex2f(h, _yMin);
                glVertex2f(h, _yMax);
                glColor3f(grey.r, grey.g, grey.b);
                glVertex2f(_xMin, s);
                glVertex2f(_xMax, s);
            }
            glEnd();
        }

        return true;
    } // draw

    virtual ~HueCorrectInteract() {}

protected:
    ParametricParam* _hueParam;
    double _xMin, _xMax;
    double _yMin, _yMax;
};

// We are lucky, there's only one hue param, so we need only one interact
// descriptor. If there were several, be would have to use a template parameter,
// as in propTester.cpp
class HueCorrectInteractDescriptor
    : public DefaultParamInteractDescriptor<HueCorrectInteractDescriptor, HueCorrectInteract>
{
public:
    virtual void describe() OVERRIDE FINAL
    {
        setColourPicking(true);
    }
};

mDeclarePluginFactory(HueCorrectPluginFactory, {ofxsThreadSuiteCheck();}, {});

void
HueCorrectPluginFactory::describe(ImageEffectDescriptor &desc)
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
    //if (!getImageEffectHostDescription()->supportsParametricParameter) {
    //  throwHostMissingSuiteException(kOfxParametricParameterSuite);
    //}
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentRGB);
#endif
}

void
HueCorrectPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                           ContextEnum context)
{
    const ImageEffectHostDescription &gHostDescription = *getImageEffectHostDescription();
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
    //srcClip->addSupportedComponent(ePixelComponentXY);
    //srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    assert(dstClip);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    //dstClip->addSupportedComponent(ePixelComponentXY);
    //dstClip->addSupportedComponent(ePixelComponentAlpha);
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
        ParametricParamDescriptor* param = desc.defineParametricParam(kParamHue);
        assert(param);
        param->setPeriodic(true);
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
        const OfxRGBColourD alpha  = {0.398979, 0.398979, 0.398979};
        param->setUIColour( kCurveSat, alpha );
        param->setUIColour( kCurveLum, alpha );
        param->setUIColour( kCurveRed, red );
        param->setUIColour( kCurveGreen, green );
        param->setUIColour( kCurveBlue, blue );
        param->setUIColour( kCurveRSup, red );
        param->setUIColour( kCurveGSup, green );
        param->setUIColour( kCurveBSup, blue );
        param->setUIColour( kCurveSatThrsh, alpha );

        // set the min/max parametric range to 0..6
        param->setRange(0.0, 6.0);
        // set the default Y range to 0..1 for all dimensions
        param->setDimensionDisplayRange(0., 1., kCurveSat);
        param->setDimensionDisplayRange(0., 1., kCurveLum);
        param->setDimensionDisplayRange(0., 1., kCurveRed);
        param->setDimensionDisplayRange(0., 1., kCurveGreen);
        param->setDimensionDisplayRange(0., 1., kCurveBlue);
        param->setDimensionDisplayRange(0., 1., kCurveRSup);
        param->setDimensionDisplayRange(0., 1., kCurveGSup);
        param->setDimensionDisplayRange(0., 1., kCurveBSup);
        param->setDimensionDisplayRange(0., 1., kCurveSatThrsh);


        int plast = param->supportsPeriodic() ? 5 : 6;
        // set a default curve
        for (int c = 0; c < kCurveNb; ++c) {
            // minimum/maximum: are these supported by OpenFX?
            param->setDimensionRange(0., c == kCurveSatThrsh ? 1. : DBL_MAX, c);
            param->setDimensionDisplayRange(0., 2., c);
            for (int p = 0; p <= plast; ++p) {
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
        param->appendOption(kParamLuminanceMathOptionRec709);
        assert(param->getNOptions() == eLuminanceMathRec2020);
        param->appendOption(kParamLuminanceMathOptionRec2020);
        assert(param->getNOptions() == eLuminanceMathACESAP0);
        param->appendOption(kParamLuminanceMathOptionACESAP0);
        assert(param->getNOptions() == eLuminanceMathACESAP1);
        param->appendOption(kParamLuminanceMathOptionACESAP1);
        assert(param->getNOptions() == eLuminanceMathCcir601);
        param->appendOption(kParamLuminanceMathOptionCcir601);
        assert(param->getNOptions() == eLuminanceMathAverage);
        param->appendOption(kParamLuminanceMathOptionAverage);
        assert(param->getNOptions() == eLuminanceMathMaximum);
        param->appendOption(kParamLuminanceMathOptionMaximum);
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
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamPremultChanged);
        param->setDefault(false);
        param->setIsSecretAndDisabled(true);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }
} // HueCorrectPluginFactory::describeInContext

ImageEffect*
HueCorrectPluginFactory::createInstance(OfxImageEffectHandle handle,
                                        ContextEnum /*context*/)
{
    return new HueCorrectPlugin(handle);
}

static HueCorrectPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)


//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
//
// HueKeyer
//
//////////////////////////////////////////////////////////////////////////////////

#define kPluginKeyerName "HueKeyerOFX"
#define kPluginKeyerGrouping "Keyer"
#define kPluginKeyerDescription \
    "Compute a key depending on hue value.\n" \
    "Hue and saturation are computed from the the source RGB values. Depending on the hue value, the various adjustment values are computed, and then applied:\n" \
    "amount: output transparency for the given hue (amount=1 means alpha=0).\n" \
    "sat_thrsh: if source saturation is below this value, the output transparency is gradually decreased."

#define kPluginKeyerIdentifier "net.sf.openfx.HueKeyer"

#define kParamKeyerHue "hue"
#define kParamKeyerHueLabel "Hue Curves"
#define kParamKeyerHueHint "Hue-dependent alpha lookup curves:\n" \
    "amount: transparency (1-alpha) amount for the given hue\n" \
    "sat_thrsh: if source saturation is below this value, transparency is decreased progressively."

#define kCurveKeyerAmount 0
#define kCurveKeyerSatThrsh 1
#define kCurveKeyerNb 2


class HueKeyerProcessorBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg;

public:
    HueKeyerProcessorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _srcImg(NULL)
    {
    }

    void setSrcImg(const Image *v) {_srcImg = v; }
};

template<class PIX, int maxValue>
static float
sampleToFloat(PIX value)
{
    return (maxValue == 1) ? value : (value / (float)maxValue);
}

template<class PIX, int maxValue>
static PIX
floatToSample(float value)
{
    if (maxValue == 1) {
        return PIX(value);
    }
    if (value <= 0) {
        return 0;
    } else if (value >= 1.) {
        return maxValue;
    }

    return PIX(value * maxValue + 0.5f);
}

template<class PIX, int maxValue>
static PIX
floatToSample(double value)
{
    if (maxValue == 1) {
        return PIX(value);
    }
    if (value <= 0) {
        return 0;
    } else if (value >= 1.) {
        return maxValue;
    }

    return PIX(value * maxValue + 0.5);
}

template <class PIX, int nComponents, int maxValue, int nbValues>
class HueKeyerProcessor
    : public HueKeyerProcessorBase
{
private:
    std::vector<double> _hue[kCurveKeyerNb];
    ParametricParam*  _hueParam;
    double _time;

public:
    // ctor
    HueKeyerProcessor(ImageEffect &instance,
                      const RenderArguments &args,
                      ParametricParam  *hueParam)
        : HueKeyerProcessorBase(instance)
        , _hueParam(hueParam)
    {
        assert(nComponents == 4);
        // build the LUT
        assert(_hueParam);
        _time = args.time;
        for (int c = 0; c < kCurveKeyerNb; ++c) {
            _hue[c].resize(nbValues + 1);
            for (int position = 0; position <= nbValues; ++position) {
                // position to evaluate the param at
                double parametricPos = 6 * double(position) / nbValues;

                // evaluate the parametric param
                double value = _hueParam->getValue(c, _time, parametricPos);

                // all the values (in HueKeyer) must be positive. We don't care if sat_thrsh goes above 1.
                value = std::max(0., value);
                // set that in the lut
                _hue[c][position] = value;
            }
        }
    }

private:
    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        assert(nComponents == 4);
        assert(_dstImg);
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                if (!srcPix) {
                    std::fill( dstPix, dstPix + 3, PIX() );
                    dstPix[3] = maxValue;
                } else {
                    std::copy(srcPix, srcPix + 3, dstPix);
                    float r = sampleToFloat<PIX, maxValue>(srcPix[0]);
                    float g = sampleToFloat<PIX, maxValue>(srcPix[1]);
                    float b = sampleToFloat<PIX, maxValue>(srcPix[2]);
                    float h, s, v;
                    Color::rgb_to_hsv( r, g, b, &h, &s, &v );
                    h = h * 6 + 1;
                    if (h > 6) {
                        h -= 6;
                    }
                    double amount = interpolate(kCurveKeyerAmount, h);
                    double sat_thrsh = interpolate(kCurveKeyerSatThrsh, h);
                    float a = 0.;
                    if (s == 0) {
                        // saturation is 0, hue is undetermined
                        a = 0.;
                    } else if (s >= sat_thrsh) {
                        a = amount;
                    } else {
                        a = amount * s / sat_thrsh;
                    }
                    std::copy(srcPix, srcPix + 3, dstPix);
                    dstPix[3] = floatToSample<PIX, maxValue>(1. - a);
                }
                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    }

    double interpolate(int c, // the curve number
                       double value)
    {
        if ( (value < 0.) || (6. < value) ) {
            // slow version
            double ret = _hueParam->getValue(c, _time, value);

            return ret;
        } else {
            double x = value / 6.;
            int i = (int)(x * nbValues);
            assert(0 <= i && i <= nbValues);
            double alpha = std::max( 0., std::min(x * nbValues - i, 1.) );
            double a = _hue[c][i];
            double b = (i  < nbValues) ? _hue[c][i + 1] : 0.f;

            return a * (1.f - alpha) + b * alpha;
        }
    }
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class HueKeyerPlugin
    : public ImageEffect
{
public:
    HueKeyerPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentAlpha ||
                             _dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentAlpha ||
                               _srcClip->getPixelComponents() == ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA) ) );

        _hue = fetchParametricParam(kParamKeyerHue);
    }

private:
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    template <int nComponents>
    void renderForComponents(const RenderArguments &args, BitDepthEnum dstBitDepth);

    void setupAndProcess(HueKeyerProcessorBase &, const RenderArguments &args);

private:
    Clip *_dstClip;
    Clip *_srcClip;
    ParametricParam  *_hue;
};


void
HueKeyerPlugin::setupAndProcess(HueKeyerProcessorBase &processor,
                                const RenderArguments &args)
{
    const double time = args.time;

    assert(_dstClip);
    auto_ptr<Image> dst( _dstClip->fetchImage(time) );
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
        PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    if ( src.get() && dst.get() ) {
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();
        BitDepthEnum dstBitDepth       = dst->getPixelDepth();
        PixelComponentEnum dstComponents  = dst->getPixelComponents();

        // see if they have the same depths and bytes and all
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );
    processor.setRenderWindow(args.renderWindow);
    processor.process();
} // HueKeyerPlugin::setupAndProcess

// the internal render function
template <int nComponents>
void
HueKeyerPlugin::renderForComponents(const RenderArguments &args,
                                    BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case eBitDepthUByte: {
        HueKeyerProcessor<unsigned char, nComponents, 255, 255> fred(*this, args, _hue);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthUShort: {
        HueKeyerProcessor<unsigned short, nComponents, 65535, 65535> fred(*this, args, _hue);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthFloat: {
        HueKeyerProcessor<float, nComponents, 1, 1023> fred(*this, args, _hue);
        setupAndProcess(fred, args);
        break;
    }
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

void
HueKeyerPlugin::render(const RenderArguments &args)
{
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    if (dstComponents == ePixelComponentRGBA) {
        renderForComponents<4>(args, dstBitDepth);
    } else {
        throwSuiteStatusException(kOfxStatErrFormat);
    }
}

mDeclarePluginFactory(HueKeyerPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
HueKeyerPluginFactory::describe(ImageEffectDescriptor &desc)
{
    desc.setLabel(kPluginKeyerName);
    desc.setPluginGrouping(kPluginKeyerGrouping);
    desc.setPluginDescription(kPluginKeyerDescription);

    desc.addSupportedContext(eContextFilter);
    //desc.addSupportedContext(eContextPaint);
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
    //if (!getImageEffectHostDescription()->supportsParametricParameter) {
    //  throwHostMissingSuiteException(kOfxParametricParameterSuite);
    //}
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

void
HueKeyerPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                         ContextEnum /*context*/)
{
    const ImageEffectHostDescription &gHostDescription = *getImageEffectHostDescription();
    const bool supportsParametricParameter = ( gHostDescription.supportsParametricParameter &&
                                               !(gHostDescription.hostName == "uk.co.thefoundry.nuke" &&
                                                 8 <= gHostDescription.versionMajor && gHostDescription.versionMajor <= 10) ); // Nuke 8-10 are known to *not* support Parametric

    if (!supportsParametricParameter) {
        throwHostMissingSuiteException(kOfxParametricParameterSuite);
    }

    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    assert(srcClip);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    //srcClip->addSupportedComponent(ePixelComponentRGB);
    //srcClip->addSupportedComponent(ePixelComponentXY);
    //srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    assert(dstClip);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    //dstClip->addSupportedComponent(ePixelComponentRGB);
    //dstClip->addSupportedComponent(ePixelComponentXY);
    //dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);


    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // define it
    {
        ParametricParamDescriptor* param = desc.defineParametricParam(kParamKeyerHue);
        assert(param);
        param->setPeriodic(true);
        param->setLabel(kParamKeyerHueLabel);
        param->setHint(kParamKeyerHueHint);
        {
            HueCorrectInteractDescriptor* interact = new HueCorrectInteractDescriptor;
            param->setInteractDescriptor(interact);
        }

        // define it as three dimensional
        param->setDimension(kCurveKeyerNb);

        // label our dimensions are r/g/b
        param->setDimensionLabel("amount", kCurveKeyerAmount);
        param->setDimensionLabel("sat_thrsh", kCurveKeyerSatThrsh);

        // set the UI colour for each dimension
        //const OfxRGBColourD master  = {0.9, 0.9, 0.9};
        // the following are magic colors, they all have the same Rec709 luminance
        //const OfxRGBColourD red   = {0.711519527404004, 0.164533420851110, 0.164533420851110};      //set red color to red curve
        //const OfxRGBColourD green = {0., 0.546986106552894, 0.};        //set green color to green curve
        //const OfxRGBColourD blue  = {0.288480472595996, 0.288480472595996, 0.835466579148890};      //set blue color to blue curve
        const OfxRGBColourD alpha  = {0.398979, 0.398979, 0.398979};
        const OfxRGBColourD yellow  = {0.711519527404004, 0.711519527404004, 0.164533420851110};
        param->setUIColour( kCurveKeyerAmount, alpha );
        param->setUIColour( kCurveKeyerSatThrsh, yellow );

        // set the min/max parametric range to 0..6
        param->setRange(0.0, 6.0);
        // set the default Y range to 0..1 for all dimensions
        param->setDimensionDisplayRange(0., 1., kCurveKeyerAmount);
        param->setDimensionDisplayRange(0., 1., kCurveKeyerSatThrsh);


        int plast = param->supportsPeriodic() ? 5 : 6;
        // set a default curve
        for (int c = 0; c < kCurveKeyerNb; ++c) {
            // minimum/maximum: are these supported by OpenFX?
            param->setDimensionRange(0., 1., c);
            param->setDimensionDisplayRange(0., 1., c);
            for (int p = 0; p <= plast; ++p) {
                // add a control point at p
                param->addControlPoint(c, // curve to set
                                       0.0,   // time, ignored in this case, as we are not adding a key
                                       p,   // parametric position, zero
                                       (c == kCurveKeyerSatThrsh) ? 0.1 : (double)(p == 3 || p == 4),   // value to be
                                       false);   // don't add a key
            }
        }

        if (page) {
            page->addChild(*param);
        }
    }
} // HueKeyerPluginFactory::describeInContext

ImageEffect*
HueKeyerPluginFactory::createInstance(OfxImageEffectHandle handle,
                                      ContextEnum /*context*/)
{
    return new HueKeyerPlugin(handle);
}

static HueKeyerPluginFactory p1(kPluginKeyerIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p1)

OFXS_NAMESPACE_ANONYMOUS_EXIT
