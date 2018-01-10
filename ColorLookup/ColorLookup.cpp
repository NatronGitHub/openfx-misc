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
 * OFX ColorLookup plugin
 */

#include <cmath>
#include <cfloat> // DBL_MAX
#include <algorithm>

#if !( defined(_WIN32) || defined(__WIN32__) || defined(WIN32) )
#define GL_GLEXT_PROTOTYPES
#endif
#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#else
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <GL/gl.h>
#include <GL/glext.h>
#endif

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#ifndef GL_VERSION_1_4
typedef void (APIENTRYP PFNGLBLENDFUNCSEPARATEPROC)(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha);
#endif
#ifndef GL_VERSION_2_0
typedef void (APIENTRYP PFNGLBLENDEQUATIONSEPARATEPROC)(GLenum modeRGB, GLenum modeAlpha);
#endif
#endif

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsCoords.h"
#include "ofxsLut.h"
#include "ofxsMacros.h"
#include "ofxsThreadSuite.h"
#ifdef OFX_USE_MULTITHREAD_MUTEX
namespace {
    typedef MultiThread::Mutex Mutex;
    typedef MultiThread::AutoMutex AutoMutex;
}
#else
// some OFX hosts do not have mutex handling in the MT-Suite (e.g. Sony Catalyst Edit)
// prefer using the fast mutex by Marcus Geelnard http://tinythreadpp.bitsnbites.eu/
#include "fast_mutex.h"
namespace {
    typedef tthread::fast_mutex Mutex;
    typedef OFX::MultiThread::AutoMutexT<tthread::fast_mutex> AutoMutex;
}
#endif

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "ColorLookupOFX"
#define kPluginGrouping "Color"
#define kPluginDescription \
    "Apply a parametric lookup curve with the possibility to adjust each channel separately.\n" \
    "The master curve is combined with the red, green and blue curves, but not with the alpha curve.\n" \
    "Different algorithms are available when applying the master curve, which are selectable using the \"Master Curve Mode\" parameter.\n" \
    "Computation is faster for values that are within the given range, so it is recommended to set the Range parameter if the input range goes beyond [0,1].\n" \
    "\n" \
    "Note that you can easily do color remapping by setting Source and Target colors and clicking \"Set RGB\" or \"Set RGBA\" below.\n" \
    "This will add control points on the curve to match the target from the source. You can add as many point as you like.\n" \
    "This is very useful for matching color of one shot to another, or adding custom colors to a black and white ramp.\n" \
    "\n" \
    "Optionally, the RGB histogram or a color ramp can be displayed in the background of the lookup curves.\n" \
    "\n" \
    "See also: http://opticalenquiry.com/nuke/index.php?title=ColorLookup"

// History:
// 1.0 initial version
// 1.1 (10/2017) add display/computation of histogram and master curve modes
#define kPluginIdentifier "net.sf.openfx.ColorLookupPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 1 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamLookupTable "lookupTable"
#define kParamLookupTableLabel "Lookup Table"
#define kParamLookupTableHint "Colour lookup table. The master curve is combined with the red, green and blue curves, but not with the alpha curve."

#define kParamSource "source"
#define kParamSourceLabel "Source"
#define kParamSourceHint "Source color for newly added points (x coordinate on the curve)."

#define kParamTarget "target"
#define kParamTargetLabel "Target"
#define kParamTargetHint "Target color for newly added points (y coordinate on the curve)."

#define kParamSetMaster "setMaster"
#define kParamSetMasterLabel "Set Master"
#define kParamSetMasterHint "Add a new control point mapping source to target to the master curve (the relative luminance is computed using the 'Luminance Math' parameter)."

#define kParamSetRGB "setRGB"
#define kParamSetRGBLabel "Set RGB"
#define kParamSetRGBHint "Add a new control point mapping source to target to the red, green, and blue curves."

#define kParamSetRGBA "setRGBA"
#define kParamSetRGBALabel "Set RGBA"
#define kParamSetRGBAHint "Add a new control point mapping source to target to the red, green, blue and alpha curves."

#define kParamSetA "setA"
#define kParamSetALabel "Set A"
#define kParamSetAHint "Add a new control point mapping source to target to the alpha curve"

#ifdef COLORLOOKUP_ADD
#define kParamAddCtrlPts "addCtrlPts"
#define kParamAddCtrlPtsLabel "Add Control Points"
#endif

#ifdef COLORLOOKUP_RESET
#define kParamResetCtrlPts "resetCtrlPts"
#define kParamResetCtrlPtsLabel "Reset"
#endif

#define kParamMasterCurveMode "masterCurveMode"
#define kParamMasterCurveModeLabel "Master Curve Mode", "Algorithm that will be used for the master curve. The curve mode will have a strong effect on the appearance of colors, especially if you use a contrast-enhancing curve (S-curve). This can be used for creative effect, but can for some purposes or styles cause undesired color changes depending which mode you choose. Choose a mode that suits your specific taste and needs for the photo at hand. More information can be found at http://rawpedia.rawtherapee.com/Exposure"
#define kParamMasterCurveModeOptionStandard "Standard", "The marster curve is applied independently to R, G and B channels. The drawback of this mode is that e.g. considering an S-curve shape to get more contrast, an orange color with a high value of red and green and a low value of blue will tend to shift toward yellow, because the red and green component will be raised, while the blue one will be lowered.", "standard"
#define kParamMasterCurveModeOptionWeightedStandard "Weighted Standard", "You can use this method to limit the color shift of the standard curve, even if it won't suppress it entirely.", "weightedstandard"
#define kParamMasterCurveModeOptionFilmLike "Film-Like", "The film-like curve provides a result highly similar to the standard type (that is strong saturation increase with increased contrast), but the RGB-HSV hue is kept constant - that is, there are less color-shift problems. This curve type was designed by Adobe as a part of DNG and is thus the one used by Adobe Camera Raw and Lightroom.", "filmlike"
#define kParamMasterCurveModeOptionLuminance "Luminance", "Each component of the pixel is boosted by the same factor so color and saturation is kept stable, that is the result is very true to the original color. However contrast-increasing curves can still lead to a slightly desaturated look. First the relative luminance value of a pixel is obtained, then the curve is applied to that value, the multiplication factor between before and after luminance is calculated, and then this factor is applied to each R, G and B component. The formula used to compute the luminance can be selected using the \"luminanceMath\" parameter.", "luminance"
enum MasterCurveModeEnum
{
    eMasterCurveModeStandard = 0,
    eMasterCurveModeWeightedStandard,
    eMasterCurveModeFilmLike,
    eMasterCurveModeLuminance,
};
#define kParamMasterCurveModeDefault eMasterCurveModeStandard

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

#define kParamHasBackgroundInteract "hasBackgroundInteract"

#define kParamShowRamp "showRamp" // left here for backward compatibility, but replaced by kParamDisplay
//#define kParamShowRampLabel "Display Color Ramp"
//#define kParamShowRampHint "Display the color ramp under the curves."

#define kParamDisplay "backgroundDisplay"
#define kParamDisplayLabel "Display", "Display a color ramp or a histogram behind the curves."
#define kParamDisplayOptionNone "None", "No background display.", "none"
#define kParamDisplayOptionColorRamp "Color Ramp", "Display a color ramp.", "colorramp"
#define kParamDisplayOptionHistogram "RGB Histogram", "Display the input histogram. Press \"Refresh Histogram\" to recompute the histogram.", "histogram"
enum DisplayEnum
{
    eDisplayNone = 0,
    eDisplayColorRamp,
    eDisplayHistogram,
};
#define kParamDisplayDefault eDisplayColorRamp

#define kParamUpdateHistogram "updateHistogram"
#define kParamUpdateHistogramLabel "Update Histogram", "Update the histogram from the input at current time."

#define kParamRange "range"
#define kParamRangeLabel "Range"
#define kParamRangeHint "Expected range for input values. Within this range, a lookup table is used for faster computation."

#define kParamClampBlack "clampBlack"
#define kParamClampBlackLabel "Clamp Black"
#define kParamClampBlackHint "All colors below 0 on output are set to 0."

#define kParamClampWhite "clampWhite"
#define kParamClampWhiteLabel "Clamp White"
#define kParamClampWhiteHint "All colors above 1 on output are set to 1."

#define kParamPremultChanged "premultChanged"

#define kCurveMaster 0
#define kCurveRed 1
#define kCurveGreen 2
#define kCurveBlue 3
#define kCurveAlpha 4
#define kCurveNb 5

static
double
luminance(double r,
          double g,
          double b,
          LuminanceMathEnum luminanceMath);

class ColorLookupProcessorBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg;
    const Image *_maskImg;
    bool _doMasking;
    const bool _clampBlack;
    const bool _clampWhite;
    bool _premult;
    int _premultChannel;
    double _mix;
    bool _maskInvert;

public:
    ColorLookupProcessorBase(ImageEffect &instance,
                             bool clampBlack,
                             bool clampWhite)
        : ImageProcessor(instance)
        , _srcImg(NULL)
        , _maskImg(NULL)
        , _doMasking(false)
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

    void setValues(bool premult,
                   int premultChannel,
                   double mix)
    {
        _premult = premult;
        _premultChannel = premultChannel;
        _mix = mix;
    }

protected:
    // clamp for integer PIX types
    template<class PIX>
    float clamp(float value,
                int maxValue) const
    {
        return std::max( 0.f, std::min( value, float(maxValue) ) );
    }

    // clamp for integer PIX types
    template<class PIX>
    double clamp(double value,
                 int maxValue) const
    {
        return std::max( 0., std::min( value, double(maxValue) ) );
    }
};


// floats don't clamp except if _clampBlack or _clampWhite
template<>
float
ColorLookupProcessorBase::clamp<float>(float value,
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
ColorLookupProcessorBase::clamp<float>(double value,
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

static inline int
componentToCurve(int comp)
{
    switch (comp) {
    case 0:

        return kCurveRed;
    case 1:

        return kCurveGreen;
    case 2:

        return kCurveBlue;
    case 3:

        return kCurveAlpha;
    default:

        return 0;
    }
}

// template to do the processing.
// nbValues is the number of values in the LUT minus 1. For integer types, it should be the same as
// maxValue
template <class PIX, int nComponents, int maxValue, int nbValues, MasterCurveModeEnum masterCurveMode>
class ColorLookupProcessor
    : public ColorLookupProcessorBase
{
public:
    // ctor
    ColorLookupProcessor(ImageEffect &instance,
                         const RenderArguments &args,
                         ParametricParam  *lookupTableParam,
                         double rangeMin,
                         double rangeMax,
                         bool clampBlack,
                         bool clampWhite,
                         LuminanceMathEnum luminanceMath)
        : ColorLookupProcessorBase(instance, clampBlack, clampWhite)
        , _lookupTableParam(lookupTableParam)
        , _rangeMin( std::min(rangeMin, rangeMax) )
        , _rangeMax( std::max(rangeMin, rangeMax) )
        , _luminanceMath( luminanceMath )
    {
        // build the LUT
        assert(_lookupTableParam);
        _time = args.time;
        if (_rangeMin == _rangeMax) {
            // avoid divisions by zero
            _rangeMax = _rangeMin + 1.;
        }
        assert( (PIX)maxValue == maxValue );
        // except for float, maxValue is the same as nbValues
        assert( maxValue == 1 || (maxValue == nbValues) );
        if (masterCurveMode == eMasterCurveModeStandard ||
            masterCurveMode == eMasterCurveModeWeightedStandard) {
            // Standard and WeightedStandard use separate R,G,B curves
            for (int component = 0; component < nComponents; ++component) {
                _lookupTable[component].resize(nbValues + 1);
                int lutIndex = nComponents == 1 ? kCurveAlpha : componentToCurve(component); // special case for components == alpha only
                for (int position = 0; position <= nbValues; ++position) {
                    // position to evaluate the param at
                    double parametricPos = _rangeMin + (_rangeMax - _rangeMin) * double(position) / nbValues;

                    // evaluate the parametric param
                    double value = _lookupTableParam->getValue(lutIndex, _time, parametricPos);
                    if ( (nComponents != 1) && (lutIndex != kCurveAlpha) ) {
                        value += _lookupTableParam->getValue(kCurveMaster, _time, parametricPos) - parametricPos;
                    }
                    // set that in the lut
                    _lookupTable[component][position] = (float)clamp<PIX>(value, maxValue);
                }
            }
        } else {
            // FilmLike and Luminance require a separate master curve
            for (int component = 0; component <= nComponents; ++component) {
                _lookupTable[component].resize(nbValues + 1);
                int lutIndex = component == nComponents ? kCurveMaster :
                                ( (nComponents == 1  && component == 0) ? kCurveAlpha :
                                 componentToCurve(component) ); // special case for components == alpha only
                for (int position = 0; position <= nbValues; ++position) {
                    // position to evaluate the param at
                    double parametricPos = _rangeMin + (_rangeMax - _rangeMin) * double(position) / nbValues;

                    // evaluate the parametric param
                    double value = _lookupTableParam->getValue(lutIndex, _time, parametricPos);
                    // set that in the lut
                    _lookupTable[component][position] = (float)clamp<PIX>(value, maxValue);
                }
            }
        }
    }

private:
    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        assert(nComponents == 1 || nComponents == 3 || nComponents == 4);
        assert(_dstImg);
        float tmpPix[4];
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                if (nComponents == 1) {
                    // Alpha: don't premult/unpremult, just apply curves
                    // normalize/denormalize properly
                    float a = srcPix ? (srcPix[0] / (float)maxValue) : 0.f;
                    tmpPix[0] = interpolate(0, a) * maxValue;
                    for (int c = 0; c < nComponents; ++c) {
                        assert( ( !srcPix || ( !OFX::IsNaN(srcPix[c]) && !OFX::IsNaN(srcPix[c]) ) ) && !OFX::IsNaN(tmpPix[c]) && !OFX::IsNaN(tmpPix[c]) );
                    }
                    // ofxsMaskMix expects denormalized input
                    ofxsMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
               } else if (nComponents == 3) {
                    // RGB: don't premult/unpremult, just apply curves
                    float r = srcPix ? (srcPix[0] / (float)maxValue) : 0.f;
                    float g = srcPix ? (srcPix[1] / (float)maxValue) : 0.f;
                    float b = srcPix ? (srcPix[2] / (float)maxValue) : 0.f;
                    // normalize/denormalize properly
                    switch (masterCurveMode) {
                        case eMasterCurveModeStandard: {
                            tmpPix[0] = interpolate(0, r) * maxValue;
                            tmpPix[1] = interpolate(1, g) * maxValue;
                            tmpPix[2] = interpolate(2, b) * maxValue;
                            break;
                        }
                        case eMasterCurveModeWeightedStandard: {
                            // see https://github.com/Beep6581/RawTherapee/blob/3ff2519302e3bc529b111462a4697ac4dfba30c4/rtengine/curves.h#L1042

                            float r1 = interpolate(0, r);
                            float g1 = Triangle(r, r1, g);
                            float b1 = Triangle(r, r1, b);

                            float g2 = interpolate(1, g);
                            float r2 = Triangle(g, g2, r);
                            float b2 = Triangle(g, g2, b);

                            float b3 = interpolate(2, b);
                            float r3 = Triangle(b, b3, r);
                            float g3 = Triangle(b, b3, g);

                            r = (r1 * 0.50f + r2 * 0.25f + r3 * 0.25f);
                            g = (g1 * 0.25f + g2 * 0.50f + g3 * 0.25f);
                            b = (b1 * 0.25f + b2 * 0.25f + b3 * 0.50f);
                            tmpPix[0] = clamp<float>(r, 1) * maxValue;
                            tmpPix[1] = clamp<float>(g, 1) * maxValue;
                            tmpPix[2] = clamp<float>(b, 1) * maxValue;
                            break;
                        }
                        case eMasterCurveModeFilmLike: {
                            // see https://github.com/Beep6581/RawTherapee/blob/3ff2519302e3bc529b111462a4697ac4dfba30c4/rtengine/curves.h#L919
                            // and https://github.com/Beep6581/RawTherapee/blob/3ff2519302e3bc529b111462a4697ac4dfba30c4/rtengine/curves.h#L974
                            double rcoef = r < 1e-8 ? 1. : (interpolate(0, r) / r);
                            double gcoef = g < 1e-8 ? 1. : (interpolate(1, g) / g);
                            double bcoef = b < 1e-8 ? 1. : (interpolate(2, b) / b);
                            if (r >= g) {
                                if (g > b) {
                                    RGBTone (r, g, b);    // Case 1: r >= g >  b
                                } else if (b > r) {
                                    RGBTone (b, r, g);    // Case 2: b >  r >= g
                                } else if (b > g) {
                                    RGBTone (r, b, g);    // Case 3: r >= b >  g
                                } else {                           // Case 4: r >= g == b
                                    r = interpolate(nComponents, r); // master curve
                                    g = interpolate(nComponents, g); // master curve
                                    b = g;
                                }
                            } else {
                                if (r >= b) {
                                    RGBTone (g, r, b);    // Case 5: g >  r >= b
                                } else if (b >  g) {
                                    RGBTone (b, g, r);    // Case 6: b >  g >  r
                                } else {
                                    RGBTone (g, b, r);    // Case 7: g >= b >  r
                                }
                            }
                            tmpPix[0] = clamp<float>(rcoef * r, 1) * maxValue;
                            tmpPix[1] = clamp<float>(gcoef * g, 1) * maxValue;
                            tmpPix[2] = clamp<float>(bcoef * b, 1) * maxValue;
                            break;
                        }
                        case eMasterCurveModeLuminance: {
                            // see https://github.com/Beep6581/RawTherapee/blob/3ff2519302e3bc529b111462a4697ac4dfba30c4/rtengine/curves.h#L992
                            double l = std::max(luminance(r, g, b, _luminanceMath), 1e-8); // avoid division by zero
                            // apply the master curve to the luminance, and
                            double coef = interpolate(nComponents, l) / l;
                            tmpPix[0] = clamp<float>(coef * interpolate(0, r), 1) * maxValue;
                            tmpPix[1] = clamp<float>(coef * interpolate(1, g), 1) * maxValue;
                            tmpPix[2] = clamp<float>(coef * interpolate(2, b), 1) * maxValue;
                            break;
                        }
                    }
                    for (int c = 0; c < nComponents; ++c) {
                        assert( ( !srcPix || ( !OFX::IsNaN(srcPix[c]) && !OFX::IsNaN(srcPix[c]) ) ) && !OFX::IsNaN(tmpPix[c]) && !OFX::IsNaN(tmpPix[c]) );
                    }
                    // ofxsMaskMix expects denormalized input
                    ofxsMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                } else {
                    //assert(nComponents == 4);
                    float unpPix[4];
                    ofxsUnPremult<PIX, nComponents, maxValue>(srcPix, unpPix, _premult, _premultChannel);
                    float r = unpPix[0];
                    float g = unpPix[1];
                    float b = unpPix[2];
                    // ofxsUnPremult outputs normalized data
                    switch (masterCurveMode) {
                        case eMasterCurveModeStandard: {
                            tmpPix[0] = interpolate(0, r);
                            tmpPix[1] = interpolate(1, g);
                            tmpPix[2] = interpolate(2, b);
                            tmpPix[3] = interpolate(3, unpPix[3]);
                            break;
                        }
                        case eMasterCurveModeWeightedStandard: {
                            // see https://github.com/Beep6581/RawTherapee/blob/3ff2519302e3bc529b111462a4697ac4dfba30c4/rtengine/curves.h#L1042

                            float r1 = interpolate(0, r);
                            float g1 = Triangle(r, r1, g);
                            float b1 = Triangle(r, r1, b);

                            float g2 = interpolate(1, g);
                            float r2 = Triangle(g, g2, r);
                            float b2 = Triangle(g, g2, b);

                            float b3 = interpolate(2, b);
                            float r3 = Triangle(b, b3, r);
                            float g3 = Triangle(b, b3, g);

                            r = (r1 * 0.50f + r2 * 0.25f + r3 * 0.25f);
                            g = (g1 * 0.25f + g2 * 0.50f + g3 * 0.25f);
                            b = (b1 * 0.25f + b2 * 0.25f + b3 * 0.50f);
                            tmpPix[0] = clamp<float>(r, 1);
                            tmpPix[1] = clamp<float>(g, 1);
                            tmpPix[2] = clamp<float>(b, 1);
                            tmpPix[3] = interpolate(3, unpPix[3]);
                            break;
                        }
                        case eMasterCurveModeFilmLike: {
                            // see https://github.com/Beep6581/RawTherapee/blob/3ff2519302e3bc529b111462a4697ac4dfba30c4/rtengine/curves.h#L919
                            // and https://github.com/Beep6581/RawTherapee/blob/3ff2519302e3bc529b111462a4697ac4dfba30c4/rtengine/curves.h#L974
                            double rcoef = r < 1e-8 ? 1. : (interpolate(0, r) / r);
                            double gcoef = g < 1e-8 ? 1. : (interpolate(1, g) / g);
                            double bcoef = b < 1e-8 ? 1. : (interpolate(2, b) / b);
                            if (r >= g) {
                                if (g > b) {
                                    RGBTone (r, g, b);    // Case 1: r >= g >  b
                                } else if (b > r) {
                                    RGBTone (b, r, g);    // Case 2: b >  r >= g
                                } else if (b > g) {
                                    RGBTone (r, b, g);    // Case 3: r >= b >  g
                                } else {                           // Case 4: r >= g == b
                                    r = interpolate(nComponents, r); // master curve
                                    g = interpolate(nComponents, g); // master curve
                                    b = g;
                                }
                            } else {
                                if (r >= b) {
                                    RGBTone (g, r, b);    // Case 5: g >  r >= b
                                } else if (b >  g) {
                                    RGBTone (b, g, r);    // Case 6: b >  g >  r
                                } else {
                                    RGBTone (g, b, r);    // Case 7: g >= b >  r
                                }
                            }
                            tmpPix[0] = clamp<float>(rcoef * r, 1);
                            tmpPix[1] = clamp<float>(gcoef * g, 1);
                            tmpPix[2] = clamp<float>(bcoef * b, 1);
                            tmpPix[3] = interpolate(3, unpPix[3]);
                            break;
                        }
                        case eMasterCurveModeLuminance: {
                            // see https://github.com/Beep6581/RawTherapee/blob/3ff2519302e3bc529b111462a4697ac4dfba30c4/rtengine/curves.h#L992
                            double l = std::max(luminance(r, g, b, _luminanceMath), 1e-8); // avoid division by zero
                            // apply the master curve to the luminance, and
                            double coef = interpolate(nComponents, l) / l;
                            tmpPix[0] = clamp<float>(coef * interpolate(0, r), 1);
                            tmpPix[1] = clamp<float>(coef * interpolate(1, g), 1);
                            tmpPix[2] = clamp<float>(coef * interpolate(2, b), 1);
                            tmpPix[3] = interpolate(3, unpPix[3]);
                            break;
                        }
                    }
                    for (int c = 0; c < nComponents; ++c) {
                        assert( !OFX::IsNaN(unpPix[c]) && !OFX::IsNaN(unpPix[c]) && !OFX::IsNaN(tmpPix[c]) && !OFX::IsNaN(tmpPix[c]) );
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
                      float value) const
    {
        if ( (value < _rangeMin) || (_rangeMax < value) ) {
            // slow version
            int lutIndex = component == nComponents ? kCurveMaster :
                            ( (nComponents == 1  && component == 0) ? kCurveAlpha :
                             componentToCurve(component) ); // special case for components == alpha only
            double ret = _lookupTableParam->getValue(lutIndex, _time, value);
            if ( (masterCurveMode == eMasterCurveModeStandard ||
                  masterCurveMode == eMasterCurveModeWeightedStandard) &&
                 (nComponents != 1) && (lutIndex != kCurveAlpha) ) {
                ret += _lookupTableParam->getValue(kCurveMaster, _time, value) - value;
            }

            return clamp<float>(ret, 1);;
        } else {
            double x = (value - _rangeMin) / (_rangeMax - _rangeMin);
            if (x <= 0.) {
                return _lookupTable[component][0];
            } else if (x >= 1.) {
                return _lookupTable[component][nbValues];
            }
            int i = (int)(x * nbValues);
            assert(0 <= i && i < nbValues);
            i = std::max( 0, std::min(i, nbValues - 1) );
            double alpha = std::max( 0., std::min(x * nbValues - i, 1.) );
            float a = _lookupTable[component][i];
            float b = _lookupTable[component][i + 1];

            return a * (1.f - alpha) + b * alpha;
        }
    }

private:
    float Triangle(float a, float a1, float b) const
    {
        if (a != b) {
            float b1;
            float a2 = a1 - a;

            if (b < a) {
                b1 = b + a2 *      b  /     a ;
            } else       {
                b1 = b + a2 * (1. - b) / (1. - a);
            }

            return b1;
        }

        return a1;
    }

    void RGBTone (float& r, float& g, float& b) const
    {
        float rold = r, gold = g, bold = b;

        r = interpolate(nComponents, rold); // master curve
        b = interpolate(nComponents, bold); // master curve
        g = b + ((r - b) * (gold - bold) / (rold - bold));
    }

private:
    std::vector<float> _lookupTable[nComponents+1]; // for Film-Like and Luminance, a separate lookup table is used for master
    ParametricParam*  _lookupTableParam;
    double _time;
    double _rangeMin;
    double _rangeMax;
    LuminanceMathEnum _luminanceMath;
};

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

    case eLuminanceMathRec2020:     // https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2087-0-201510-I!!PDF-E.pdf

        return Color::rgb2020_to_y(r, g, b);
    case eLuminanceMathACESAP0:     // https://en.wikipedia.org/wiki/Academy_Color_Encoding_System#Converting_ACES_RGB_values_to_CIE_XYZ_values

        return Color::rgbACESAP0_to_y(r, g, b);
    case eLuminanceMathACESAP1:     // https://en.wikipedia.org/wiki/Academy_Color_Encoding_System#Converting_ACES_RGB_values_to_CIE_XYZ_values

        return Color::rgbACESAP1_to_y(r, g, b);
    case eLuminanceMathCcir601:

        return 0.2989 * r + 0.5866 * g + 0.1145 * b;
    case eLuminanceMathAverage:

        return (r + g + b) / 3;
    case eLuminanceMathMaximum:

        return std::max(std::max(r, g), b);
    }
}

#define HISTOGRAM_BINS 256

struct RGBAValues
{
    double r, g, b, a;
    RGBAValues(double v) : r(v), g(v), b(v), a(v) {}

    RGBAValues() : r(0), g(0), b(0), a(0) {}
};

struct Results
{
    Results()
    : rangemin(0.)
    , rangemax(0.)
    , histogram()
    , bins(0)
    , components(0)
    , hmax(0)
    {
    }

    double rangemin;
    double rangemax;
    std::vector<unsigned long> histogram;
    int bins;
    int components;
    unsigned long hmax;
};

class HistogramProcessorBase
: public ImageProcessor
{
protected:
    Mutex _mutex; //< this is used so we can multi-thread the analysis and protect the shared results
    unsigned long _count;

public:
    HistogramProcessorBase(ImageEffect &instance)
    : ImageProcessor(instance)
    , _mutex()
    , _count(0)
    {
    }

    virtual ~HistogramProcessorBase()
    {
    }

    virtual void getResults(Results *results) const = 0;
};


template <class PIX, int nComponents, int maxValue>
class HistogramProcessor
: public HistogramProcessorBase
{
private:
    std::vector<unsigned long> _histogram;
    double _rangemin;
    double _rangemax;
    bool _premult;
    int _premultChannel;

public:
    HistogramProcessor(ImageEffect &instance, double rangemin, double rangemax, bool premult, int premultChannel)
    : HistogramProcessorBase(instance)
    , _histogram(HISTOGRAM_BINS * nComponents)
    , _rangemin(rangemin)
    , _rangemax(rangemax)
    , _premult(premult)
    , _premultChannel(premultChannel)
    {
    }

    ~HistogramProcessor()
    {
    }

    void getResults(Results *results) const OVERRIDE FINAL
    {
        results->rangemin = _rangemin;
        results->rangemax = _rangemax;
        results->histogram = _histogram;
        results->bins = HISTOGRAM_BINS;
        results->components = nComponents;
        unsigned long hmax = 0;
        // compute the max, excluding the first and last bins
        for (int c = 0; c < nComponents; ++c) {
            for (int i = 1; i < HISTOGRAM_BINS - 1; ++i) {
                unsigned long h = _histogram[c * HISTOGRAM_BINS + i];
                if (h > hmax) {
                    hmax = h;
                }
            }
        }
        results->hmax = hmax;
    }

private:

    void addResults(const std::vector<unsigned long>& histogram,
                    unsigned long count)
    {
        AutoMutex l (&_mutex);
        assert(histogram.size() == _histogram.size());
        for (size_t i = 0; i < HISTOGRAM_BINS * nComponents; ++i) {
            _histogram[i] += histogram[i];
        }
        _count += count;
    }

    void multiThreadProcessImages(OfxRectI procWindow) OVERRIDE FINAL
    {
        std::vector<unsigned long> histogram(HISTOGRAM_BINS * nComponents);
        unsigned long count = 0;
        assert(_dstImg->getBounds().x1 <= procWindow.x1 && procWindow.y2 <= _dstImg->getBounds().y2 &&
               _dstImg->getBounds().y1 <= procWindow.y1 && procWindow.y2 <= _dstImg->getBounds().y2);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                float unpPix[4];
                ofxsUnPremult<PIX, nComponents, maxValue>(dstPix, unpPix, _premult, _premultChannel);

                for (int c = 0; c < std::min(nComponents, 3); ++c) {
                    int bin = 0;
                    if (unpPix[c] >= _rangemax) {
                        bin = HISTOGRAM_BINS - 1;
                    } else if (unpPix[c] >= _rangemin) {
                        bin = std::floor(HISTOGRAM_BINS * (unpPix[c] - _rangemin) / (_rangemax - _rangemin));
                    }
                    ++histogram[c * HISTOGRAM_BINS + bin];
                }
                dstPix += nComponents;
            }
            count += procWindow.x2 - procWindow.x1;
        }

        addResults(histogram, count);
    }
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ColorLookupPlugin
    : public ImageEffect
{
public:
    ColorLookupPlugin(OfxImageEffectHandle handle)
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
        _hasBackgroundInteract = fetchBooleanParam(kParamHasBackgroundInteract);
        _lookupTable = fetchParametricParam(kParamLookupTable);
        _display = fetchChoiceParam(kParamDisplay);
        _updateHistogram = fetchPushButtonParam(kParamUpdateHistogram);
        _range = fetchDouble2DParam(kParamRange);
        assert(_hasBackgroundInteract && _lookupTable && _display && _updateHistogram && _range);
        _source = fetchRGBAParam(kParamSource);
        _target = fetchRGBAParam(kParamTarget);
        assert(_source && _target);
        _masterCurveMode = fetchChoiceParam(kParamMasterCurveMode);
        assert(_masterCurveMode);
        _luminanceMath = fetchChoiceParam(kParamLuminanceMath);
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

        bool hasBackgroundInteract = _hasBackgroundInteract->getValue();
        _display->setIsSecretAndDisabled(!hasBackgroundInteract);
        _updateHistogram->setIsSecret(!hasBackgroundInteract);
        if (hasBackgroundInteract) {
            DisplayEnum display = (DisplayEnum)_display->getValue();
            _updateHistogram->setEnabled(display == eDisplayHistogram);
        } else {
            _updateHistogram->setEnabled(false);
        }
    }

    void getHistogram(Results* histogram)
    {
        assert(histogram != NULL);
        AutoMutex l(&_histogramMutex);
        *histogram = _histogram;
    }

private:
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    template <int nComponents>
    void renderForComponents(const RenderArguments &args, BitDepthEnum dstBitDepth);

    void setupAndProcess(ColorLookupProcessorBase &, const RenderArguments &args);

    virtual void changedParam(const InstanceChangedArgs &args,
                              const std::string &paramName) OVERRIDE FINAL;

    // methods related to Histogram

    void updateHistogram(const InstanceChangedArgs &args);

    /* set up and run a processor */
    void
    setupAndProcess(HistogramProcessorBase &processor,
                    const Image* srcImg,
                    const OfxRectI &analysisWindow,
                    Results *results)
    {
        // set the images
        processor.setDstImg( const_cast<Image*>(srcImg) ); // not a bug: we only set dst

        // set the render window
        processor.setRenderWindow(analysisWindow);

        // Call the base class process member, this will call the derived templated process code
        processor.process();

        if ( !abort() ) {
            processor.getResults(results);
        }
    }

    // update image statistics
    void
    update(const Image* srcImg,
           double time,
           const OfxRectI &analysisWindow)
    {
        // TODO: CHECK if checkDoubleAnalysis param is true and analysisWindow is the same as btmLeft/sizeAnalysis
        Results results;

        if ( !abort() ) {
            updateSub<HistogramProcessor>(srcImg, time, analysisWindow, &results);
        }
        {
            AutoMutex l (&_histogramMutex);
            _histogram = results; // uses default copy constructor
        }
    }

    template <template<class PIX, int nComponents, int maxValue> class Processor, class PIX, int nComponents, int maxValue>
    void updateSubComponentsDepth(const Image* srcImg,
                                  double time,
                                  const OfxRectI &analysisWindow,
                                  Results* results)
    {
        double rangemin, rangemax;
        _range->getValueAtTime(time, rangemin, rangemax);
        bool premult;
        int premultChannel;
        _premult->getValueAtTime(time, premult);
        _premultChannel->getValueAtTime(time, premultChannel);
        Processor<PIX, nComponents, maxValue> fred(*this, rangemin, rangemax, premult, premultChannel);
        setupAndProcess(fred, srcImg, analysisWindow, results);
    }

    template <template<class PIX, int nComponents, int maxValue> class Processor, int nComponents>
    void updateSubComponents(const Image* srcImg,
                             double time,
                             const OfxRectI &analysisWindow,
                             Results* results)
    {
        BitDepthEnum srcBitDepth = srcImg->getPixelDepth();

        switch (srcBitDepth) {
            case eBitDepthUByte: {
                updateSubComponentsDepth<Processor, unsigned char, nComponents, 255>(srcImg, time, analysisWindow, results);
                break;
            }
            case eBitDepthUShort: {
                updateSubComponentsDepth<Processor, unsigned short, nComponents, 65535>(srcImg, time, analysisWindow, results);
                break;
            }
            case eBitDepthFloat: {
                updateSubComponentsDepth<Processor, float, nComponents, 1>(srcImg, time, analysisWindow, results);
                break;
            }
            default:
                throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }

    template <template<class PIX, int nComponents, int maxValue> class Processor>
    void updateSub(const Image* srcImg,
                   double time,
                   const OfxRectI &analysisWindow,
                   Results* results)
    {
        PixelComponentEnum srcComponents  = srcImg->getPixelComponents();

        assert(srcComponents == ePixelComponentAlpha || srcComponents == ePixelComponentRGB || srcComponents == ePixelComponentRGBA);
        if (srcComponents == ePixelComponentAlpha) {
            updateSubComponents<Processor, 1>(srcImg, time, analysisWindow, results);
        } else if (srcComponents == ePixelComponentRGBA) {
            updateSubComponents<Processor, 4>(srcImg, time, analysisWindow, results);
        } else if (srcComponents == ePixelComponentRGB) {
            updateSubComponents<Processor, 3>(srcImg, time, analysisWindow, results);
        } else {
            // coverity[dead_error_line]
            throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }


private:
    Clip *_dstClip;
    Clip *_srcClip;
    Clip *_maskClip;
    BooleanParam* _hasBackgroundInteract;
    ParametricParam  *_lookupTable;
    ChoiceParam* _display;
    PushButtonParam* _updateHistogram;
    Double2DParam* _range;
    RGBAParam* _source;
    RGBAParam* _target;
    ChoiceParam* _masterCurveMode;
    ChoiceParam* _luminanceMath;
    BooleanParam* _clampBlack;
    BooleanParam* _clampWhite;
    BooleanParam* _premult;
    ChoiceParam* _premultChannel;
    DoubleParam* _mix;
    BooleanParam* _maskApply;
    BooleanParam* _maskInvert;
    BooleanParam* _premultChanged; // set to true the first time the user connects src
    Mutex _histogramMutex; //< this is used so we can multi-thread the analysis and protect the shared results
    Results _histogram;
};

void
ColorLookupPlugin::setupAndProcess(ColorLookupProcessorBase &processor,
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
    bool premult;
    int premultChannel;
    _premult->getValueAtTime(time, premult);
    _premultChannel->getValueAtTime(time, premultChannel);
    double mix;
    _mix->getValueAtTime(time, mix);
    processor.setValues(premult, premultChannel, mix);
    processor.process();
} // ColorLookupPlugin::setupAndProcess

// the internal render function
template <int nComponents>
void
ColorLookupPlugin::renderForComponents(const RenderArguments &args,
                                       BitDepthEnum dstBitDepth)
{
    double rangeMin, rangeMax;
    const double time = args.time;

    _range->getValueAtTime(time, rangeMin, rangeMax);
    bool clampBlack = _clampBlack->getValueAtTime(time);
    bool clampWhite = _clampWhite->getValueAtTime(time);
    LuminanceMathEnum luminanceMath = (LuminanceMathEnum)_luminanceMath->getValueAtTime(time);
    MasterCurveModeEnum masterCurveMode = (MasterCurveModeEnum)_masterCurveMode->getValueAtTime(time);
    auto_ptr<ColorLookupProcessorBase> proc;
    switch (masterCurveMode) {
        case eMasterCurveModeStandard: {
            switch (dstBitDepth) {
                case eBitDepthUByte: {
                    proc.reset( new ColorLookupProcessor<unsigned char, nComponents, 255, 255, eMasterCurveModeStandard>(*this, args, _lookupTable, rangeMin, rangeMax, clampBlack, clampWhite, luminanceMath) );
                    break;
                }
                case eBitDepthUShort: {
                    proc.reset( new ColorLookupProcessor<unsigned short, nComponents, 65535, 65535, eMasterCurveModeStandard>(*this, args, _lookupTable, rangeMin, rangeMax, clampBlack, clampWhite, luminanceMath) );
                    break;
                }
                case eBitDepthFloat: {
                    proc.reset( new ColorLookupProcessor<float, nComponents, 1, 1023, eMasterCurveModeStandard>(*this, args, _lookupTable, rangeMin, rangeMax, clampBlack, clampWhite, luminanceMath) );
                    break;
                }
                default:
                    throwSuiteStatusException(kOfxStatErrUnsupported);
            }
            break;
        }
        case eMasterCurveModeWeightedStandard: {
            switch (dstBitDepth) {
                case eBitDepthUByte: {
                    proc.reset( new ColorLookupProcessor<unsigned char, nComponents, 255, 255, eMasterCurveModeWeightedStandard>(*this, args, _lookupTable, rangeMin, rangeMax, clampBlack, clampWhite, luminanceMath) );
                    break;
                }
                case eBitDepthUShort: {
                    proc.reset( new ColorLookupProcessor<unsigned short, nComponents, 65535, 65535, eMasterCurveModeWeightedStandard>(*this, args, _lookupTable, rangeMin, rangeMax, clampBlack, clampWhite, luminanceMath) );
                    break;
                }
                case eBitDepthFloat: {
                    proc.reset( new ColorLookupProcessor<float, nComponents, 1, 1023, eMasterCurveModeWeightedStandard>(*this, args, _lookupTable, rangeMin, rangeMax, clampBlack, clampWhite, luminanceMath) );
                    break;
                }
                default:
                    throwSuiteStatusException(kOfxStatErrUnsupported);
            }
            break;
        }
        case eMasterCurveModeFilmLike: {
            switch (dstBitDepth) {
                case eBitDepthUByte: {
                    proc.reset( new ColorLookupProcessor<unsigned char, nComponents, 255, 255, eMasterCurveModeFilmLike>(*this, args, _lookupTable, rangeMin, rangeMax, clampBlack, clampWhite, luminanceMath) );
                    break;
                }
                case eBitDepthUShort: {
                    proc.reset( new ColorLookupProcessor<unsigned short, nComponents, 65535, 65535, eMasterCurveModeFilmLike>(*this, args, _lookupTable, rangeMin, rangeMax, clampBlack, clampWhite, luminanceMath) );
                    break;
                }
                case eBitDepthFloat: {
                    proc.reset( new ColorLookupProcessor<float, nComponents, 1, 1023, eMasterCurveModeFilmLike>(*this, args, _lookupTable, rangeMin, rangeMax, clampBlack, clampWhite, luminanceMath) );
                    break;
                }
                default:
                    throwSuiteStatusException(kOfxStatErrUnsupported);
            }
            break;
        }
        case eMasterCurveModeLuminance: {
            switch (dstBitDepth) {
                case eBitDepthUByte: {
                    proc.reset( new ColorLookupProcessor<unsigned char, nComponents, 255, 255, eMasterCurveModeLuminance>(*this, args, _lookupTable, rangeMin, rangeMax, clampBlack, clampWhite, luminanceMath) );
                    break;
                }
                case eBitDepthUShort: {
                    proc.reset( new ColorLookupProcessor<unsigned short, nComponents, 65535, 65535, eMasterCurveModeLuminance>(*this, args, _lookupTable, rangeMin, rangeMax, clampBlack, clampWhite, luminanceMath) );
                    break;
                }
                case eBitDepthFloat: {
                    proc.reset( new ColorLookupProcessor<float, nComponents, 1, 1023, eMasterCurveModeLuminance>(*this, args, _lookupTable, rangeMin, rangeMax, clampBlack, clampWhite, luminanceMath) );
                    break;
                }
                default:
                    throwSuiteStatusException(kOfxStatErrUnsupported);
            }
            break;
        }
    }
    if (!proc.get()) {
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
    setupAndProcess(*proc, args);
}

void
ColorLookupPlugin::render(const RenderArguments &args)
{
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    if (dstComponents == ePixelComponentRGBA) {
        renderForComponents<4>(args, dstBitDepth);
    } else if (dstComponents == ePixelComponentRGB) {
        renderForComponents<3>(args, dstBitDepth);
    } else if (dstComponents == ePixelComponentXY) {
        renderForComponents<2>(args, dstBitDepth);
    } else {
        assert(dstComponents == ePixelComponentAlpha);
        renderForComponents<1>(args, dstBitDepth);
    }
}

bool
ColorLookupPlugin::isIdentity(const IsIdentityArguments &args,
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
ColorLookupPlugin::changedClip(const InstanceChangedArgs &args,
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

void
ColorLookupPlugin::updateHistogram(const InstanceChangedArgs &args)
{
    auto_ptr<Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                             _srcClip->fetchImage(args.time) : 0 );
    if ( src.get() ) {
        if ( (src->getRenderScale().x != args.renderScale.x) ||
            ( src->getRenderScale().y != args.renderScale.y) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
#     ifdef kOfxImageEffectPropInAnalysis // removed from OFX 1.4
        getPropertySet().propSetInt(kOfxImageEffectPropInAnalysis, 1, false);
#     endif
        beginEditBlock("analyzeFrame");
        update(src.get(), args.time, src->getBounds());
        endEditBlock();
#     ifdef kOfxImageEffectPropInAnalysis // removed from OFX 1.4
        getPropertySet().propSetInt(kOfxImageEffectPropInAnalysis, 0, false);
#     endif
    }
}

void
ColorLookupPlugin::changedParam(const InstanceChangedArgs &args,
                                const std::string &paramName)
{
    const double time = args.time;

    if ( paramName == kParamUpdateHistogram && _srcClip && _srcClip->isConnected() ) {
        updateHistogram(args);
    }
    if ( (paramName == kParamHasBackgroundInteract) || (paramName == kParamDisplay)) {
        bool hasBackgroundInteract = _hasBackgroundInteract->getValueAtTime(time);
        _display->setIsSecretAndDisabled(!hasBackgroundInteract);
        _updateHistogram->setIsSecret(!hasBackgroundInteract);
        if (!hasBackgroundInteract) {
            _updateHistogram->setEnabled(false);
        } else {
            DisplayEnum display = (DisplayEnum)_display->getValue();
            _updateHistogram->setEnabled(display == eDisplayHistogram);
            if ( (display == eDisplayHistogram) && _histogram.histogram.empty() ) {
                updateHistogram(args); // compute the histogram (which is not persistent)
            }
        }
    }
    if ( (paramName == kParamSetMaster) && (args.reason == eChangeUserEdit) ) {
        double source[4];
        double target[4];
        _source->getValueAtTime(time, source[0], source[1], source[2], source[3]);
        _target->getValueAtTime(time, target[0], target[1], target[2], target[3]);
        LuminanceMathEnum luminanceMath = (LuminanceMathEnum)_luminanceMath->getValueAtTime(time);
        double s = luminance(source[0], source[1], source[2], luminanceMath);
        double t = luminance(target[0], target[1], target[2], luminanceMath);
        _lookupTable->addControlPoint(kCurveMaster, // curve to set
                                      time,   // time, ignored in this case, as we are not adding a key
                                      s,   // parametric position
                                      t,   // value to be
                                      false);   // don't add a key
    }
    if ( ( (paramName == kParamSetRGB) || (paramName == kParamSetRGBA) || (paramName == kParamSetA) ) && (args.reason == eChangeUserEdit) ) {
        double source[4];
        double target[4];
        _source->getValueAtTime(time, source[0], source[1], source[2], source[3]);
        _target->getValueAtTime(time, target[0], target[1], target[2], target[3]);

        int cbegin = (paramName == kParamSetA) ? 3 : 0;
        int cend = (paramName == kParamSetRGB) ? 3 : 4;
        for (int c = cbegin; c < cend; ++c) {
            int curve = componentToCurve(c);
            _lookupTable->addControlPoint(curve, // curve to set
                                          time,   // time, ignored in this case, as we are not adding a key
                                          source[c],   // parametric position
                                          target[c],   // value to be
                                          false);   // don't add a key
        }
    }
#ifdef COLORLOOKUP_ADD
    if ( (paramName == kParamAddCtrlPts) && (args.reason == eChangeUserEdit) ) {
        for (int component = 0; component < kCurveNb; ++component) {
            int n = _lookupTable->getNControlPoints(component, time);
            if (n <= 1) {
                // less than two points: add the two default control points
                // add a control point at 0, value is 0
                _lookupTable->addControlPoint(component, // curve to set
                                              time,  // time, ignored in this case, as we are not adding a key
                                              0.0,  // parametric position, zero
                                              0.0,  // value to be, 0
                                              false);  // don't add a key
                // add a control point at 1, value is 1
                _lookupTable->addControlPoint(component, time, 1.0, 1.0, false);
            } else {
                std::pair<double, double> prev = _lookupTable->getNthControlPoint(component, time, 0);
                std::list<std::pair<double, double> > newCtrlPts;

                // compute new points, put them in a list
                for (int i = 1; i < n; ++i) {
                    std::pair<double, double> next = _lookupTable->getNthControlPoint(component, time, i);
                    if (prev.first != next.first) { // don't create additional points if there is no space for one
                        // create a new control point between two existing control points
                        double parametricPos = (prev.first + next.first) / 2.;
                        double parametricVal = _lookupTable->getValueAtTime(time, component, time, parametricPos);
                        newCtrlPts.push_back( std::make_pair(parametricPos, parametricVal) );
                    }
                    prev = next;
                }
                // now add the new points
                for (std::list<std::pair<double, double> >::const_iterator it = newCtrlPts.begin();
                     it != newCtrlPts.end();
                     ++it) {
                    _lookupTable->addControlPoint(component, // curve to set
                                                  time,   // time, ignored in this case, as we are not adding a key
                                                  it->first,   // parametric position
                                                  it->second,   // value to be, 0
                                                  false);
                }
            }
        }
    }
#endif
#ifdef COLORLOOKUP_RESET
    if ( (paramName == kParamResetCtrlPts) && (args.reason == eChangeUserEdit) ) {
        Message::MessageReplyEnum reply = sendMessage(Message::eMessageQuestion, "", "Delete all control points for all components?");
        // Nuke seems to always reply eMessageReplyOK, whatever the real answer was
        switch (reply) {
            case Message::eMessageReplyOK:
                sendMessage(Message::eMessageMessage, "", "OK");
                break;
            case Message::eMessageReplyYes:
                sendMessage(Message::eMessageMessage, "", "Yes");
                break;
            case Message::eMessageReplyNo:
                sendMessage(Message::eMessageMessage, "", "No");
                break;
            case Message::eMessageReplyFailed:
                sendMessage(Message::eMessageMessage, "", "Failed");
                break;
        }
        if (reply == Message::eMessageReplyYes) {
            for (int component = 0; component < kCurveNb; ++component) {
                _lookupTable->deleteControlPoint(component);
                // add a control point at 0, value is 0
                _lookupTable->addControlPoint(component, // curve to set
                                              time,  // time, ignored in this case, as we are not adding a key
                                              0.0,  // parametric position, zero
                                              0.0,  // value to be, 0
                                              false);  // don't add a key
                // add a control point at 1, value is 1
                lookupTable->addControlPoint(component, time, 1.0, 1.0, false);
            }
        }
    }
#endif
    if ( (paramName == kParamRange) && (args.reason == eChangeUserEdit) ) {
        double rmin, rmax;
        _range->getValueAtTime(time, rmin, rmax);
        if (rmax < rmin) {
            _range->setValue(rmax, rmin);
        }
    } else if ( (paramName == kParamPremult) && (args.reason == eChangeUserEdit) ) {
        _premultChanged->setValue(true);
    }
} // changedParam

class ColorLookupInteract
    : public ParamInteract
{
public:
    ColorLookupInteract(OfxInteractHandle handle,
                        ImageEffect* effect,
                        const std::string& paramName)
        : ParamInteract(handle, effect)
    {
        _hasBackgroundInteract = effect->fetchBooleanParam(kParamHasBackgroundInteract);
        _display = effect->fetchChoiceParam(kParamDisplay);
        _updateHistogram = effect->fetchPushButtonParam(kParamUpdateHistogram);
        _lookupTableParam = effect->fetchParametricParam(paramName);
        _range = effect->fetchDouble2DParam(kParamRange);
        setColourPicking(true); // we always want colour picking if the host has it
        addParamToSlaveTo(_display);
        addParamToSlaveTo(_updateHistogram);
        addParamToSlaveTo(_hasBackgroundInteract);
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
        glBlendEquationSeparate = (PFNGLBLENDEQUATIONSEPARATEPROC)wglGetProcAddress("glBlendEquationSeparate");
        glBlendFuncSeparate = (PFNGLBLENDFUNCSEPARATEPROC)wglGetProcAddress("glBlendFuncSeparate");
#endif
    }

    virtual bool draw(const DrawArgs &args) OVERRIDE FINAL
    {
        const double time = args.time;
        bool hasBackgroundInteract = _hasBackgroundInteract->getValueAtTime(time);

        if (!hasBackgroundInteract) {
            _hasBackgroundInteract->setValue(true);
        }

        double rangeMin, rangeMax;

        _range->getValueAtTime(time, rangeMin, rangeMax);


        DisplayEnum display = (DisplayEnum)_display->getValueAtTime(time);

        if ( display == eDisplayColorRamp  ) {
            OfxStatus stat = kOfxStatOK;
            const int sliceWidth = 8;
            int nbValues = args.pixelScale.x > 0 ? std::ceil( (rangeMax - rangeMin) / (sliceWidth * args.pixelScale.x) ) : 1;
            if (nbValues > 0) {
                // let us draw one slice every 8 pixels
                const int nComponents = 3;
                GLfloat color[nComponents];
                glBegin(GL_TRIANGLE_STRIP);
                try { // getValue may throw
                    for (int position = 0; position <= nbValues; ++position) {
                        // position to evaluate the param at
                        double parametricPos = rangeMin + (rangeMax - rangeMin) * double(position) / nbValues;

                        for (int component = 0; component < nComponents; ++component) {
                            int lutIndex = componentToCurve(component); // special case for components == alpha only
                            // evaluate the parametric param
                            double value = _lookupTableParam->getValue(lutIndex, time, parametricPos);
                            value += _lookupTableParam->getValue(kCurveMaster, time, parametricPos) - parametricPos;
                            // set that in the lut
                            color[component] = value;
                        }
                        glColor3f(color[0], color[1], color[2]);
                        glVertex2f(parametricPos, rangeMin);
                        glVertex2f(parametricPos, rangeMax);
                    }
                } catch (...) {
                    stat = kOfxStatFailed;
                }
                glEnd();
            }
            throwSuiteStatusException(stat);
        }

        if ( display == eDisplayHistogram  ) {
            Results histogram;
            ColorLookupPlugin* effect = dynamic_cast<ColorLookupPlugin*>(_effect);
            if (effect) {
                effect->getHistogram(&histogram); // copy the histogram
            }
            if (histogram.hmax > 0 && histogram.rangemin < histogram.rangemax && !histogram.histogram.empty()) {
                double binSize = (histogram.rangemax - histogram.rangemin) / HISTOGRAM_BINS;
                glEnable(GL_BLEND);
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
                if (glBlendEquationSeparate) {
                    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
                }
                if (glBlendFuncSeparate) {
                    glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE);
                }
#else
                glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
                glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE);
#endif
                for (int c = 0; c < 3; ++c) {
                    glBegin(GL_QUADS);
                    // use three colors with equal luminance (0.33), so that the blue is visible and their sum is white
                    // we divide by two to get 50% white.
                    switch (c) {
                        case 0:
                            glColor3f(0.711519527404004/2, 0.164533420851110/2, 0.164533420851110/2);
                            break;
                        case 1:
                            glColor3f(0./2, 0.546986106552894/2, 0./2);
                            break;
                        case 2:
                            glColor3f(0.288480472595996/2, 0.288480472595996/2, 0.835466579148890/2);
                            break;
                    }
                    for (unsigned int i = 0; i < HISTOGRAM_BINS; ++i) {
                        double binMinX = histogram.rangemin + i * binSize;
                        double binMaxX = binMinX + binSize;
                        double binY = histogram.histogram[c* HISTOGRAM_BINS + i] / (double)histogram.hmax;
                        glVertex2d(binMinX, 0);
                        glVertex2d(binMinX, binY);
                        glVertex2d(binMaxX, binY);
                        glVertex2d(binMaxX, 0);
                    }
                    glEnd(); // GL_QUADS
                }
            }
        }

        if (args.hasPickerColour) {
            //glEnable(GL_BLEND);
            //glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
            //glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE);
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
    } // draw

    virtual ~ColorLookupInteract() {}

protected:
    BooleanParam* _hasBackgroundInteract;
    ChoiceParam* _display;
    PushButtonParam* _updateHistogram;
    ParametricParam* _lookupTableParam;
    Double2DParam* _range;
private:
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
    PFNGLBLENDEQUATIONSEPARATEPROC glBlendEquationSeparate;
    PFNGLBLENDFUNCSEPARATEPROC glBlendFuncSeparate;
#endif
};

// We are lucky, there's only one lookupTable param, so we need only one interact
// descriptor. If there were several, be would have to use a template parameter,
// as in propTester.cpp
class ColorLookupInteractDescriptor
    : public DefaultParamInteractDescriptor<ColorLookupInteractDescriptor, ColorLookupInteract>
{
public:
    virtual void describe() OVERRIDE FINAL
    {
        setColourPicking(true);
    }
};

mDeclarePluginFactory(ColorLookupPluginFactory, {ofxsThreadSuiteCheck();}, {});

void
ColorLookupPluginFactory::describe(ImageEffectDescriptor &desc)
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
    desc.setChannelSelector(ePixelComponentRGBA);
#endif
}

void
ColorLookupPluginFactory::describeInContext(ImageEffectDescriptor &desc,
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
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamHasBackgroundInteract);
        param->setDefault(false);
        param->setIsSecretAndDisabled(true);
        param->setIsPersistent(true);
        param->setEvaluateOnChange(false);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamRange);
        param->setLabel(kParamRangeLabel);
        param->setDimensionLabels("min", "max");
        param->setHint(kParamRangeHint);
        param->setDefault(0., 1.);
        param->setDoubleType(eDoubleTypePlain);
        param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0., 0., 1., 1.);
        param->setUseHostNativeOverlayHandle(false);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        ParametricParamDescriptor* param = desc.defineParametricParam(kParamLookupTable);
        assert(param);
        param->setLabel(kParamLookupTableLabel);
        param->setHint(kParamLookupTableHint);
        {
            ColorLookupInteractDescriptor* interact = new ColorLookupInteractDescriptor;
            param->setInteractDescriptor(interact);
        }

        // define it as three dimensional
        param->setDimension(kCurveNb);

        // label our dimensions are r/g/b
        param->setDimensionLabel("master", kCurveMaster);
        param->setDimensionLabel("red", kCurveRed);
        param->setDimensionLabel("green", kCurveGreen);
        param->setDimensionLabel("blue", kCurveBlue);
        param->setDimensionLabel("alpha", kCurveAlpha);

        // set the UI colour for each dimension
        const OfxRGBColourD master  = {0.9, 0.9, 0.9};
        // the following are magic colors, they all have the same Rec709 luminance
        const OfxRGBColourD red   = {0.711519527404004, 0.164533420851110, 0.164533420851110};      //set red color to red curve
        const OfxRGBColourD green = {0., 0.546986106552894, 0.};        //set green color to green curve
        const OfxRGBColourD blue  = {0.288480472595996, 0.288480472595996, 0.835466579148890};      //set blue color to blue curve
        const OfxRGBColourD alpha  = {0.398979, 0.398979, 0.398979};
        param->setUIColour( kCurveRed, red );
        param->setUIColour( kCurveGreen, green );
        param->setUIColour( kCurveBlue, blue );
        param->setUIColour( kCurveAlpha, alpha );
        param->setUIColour( kCurveMaster, master );

        // set the min/max parametric range to 0..1
        param->setRange(0.0, 1.0);
        // set the default Y range to 0..1 for all dimensions
        param->setDimensionDisplayRange(0., 1., kCurveMaster);
        param->setDimensionDisplayRange(0., 1., kCurveRed);
        param->setDimensionDisplayRange(0., 1., kCurveGreen);
        param->setDimensionDisplayRange(0., 1., kCurveBlue);
        param->setDimensionDisplayRange(0., 1., kCurveAlpha);

        /*
           // set a default curve, this example sets identity
           for (int component = 0; component < 3; ++component) {
           // add a control point at 0, value is 0
           param->addControlPoint(component, // curve to set
           0.0,   // time, ignored in this case, as we are not adding a key
           0.0,   // parametric position, zero
           0.0,   // value to be, 0
           false);   // don't add a key
           // add a control point at 1, value is 1
           param->addControlPoint(component, 0.0, 1.0, 1.0, false);
           }
         */
        param->setIdentity();
        if (page) {
            page->addChild(*param);
        }
    }
    {
        // dummy parameter, for backward compat with version 1.0
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamShowRamp);
        param->setIsSecretAndDisabled(true);
        param->setEvaluateOnChange(false);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamDisplay);
        param->setLabelAndHint(kParamDisplayLabel);
        assert(param->getNOptions() == eDisplayNone);
        param->appendOption(kParamDisplayOptionNone);
        assert(param->getNOptions() == eDisplayColorRamp);
        param->appendOption(kParamDisplayOptionColorRamp);
        assert(param->getNOptions() == eDisplayHistogram);
        param->appendOption(kParamDisplayOptionHistogram);
        param->setDefault((int)kParamDisplayDefault);
        param->setEvaluateOnChange(false);
        param->setAnimates(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
    }
    {
        PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamUpdateHistogram);
        param->setLabelAndHint(kParamUpdateHistogramLabel);
    }
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamSource);
        param->setLabel(kParamSourceLabel);
        param->setHint(kParamSourceHint);
        param->setRange(-DBL_MAX, -DBL_MAX, -DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0., 0., 0., 0., 4., 4., 4., 4.);
        param->setEvaluateOnChange(false);
        param->setIsPersistent(false);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamTarget);
        param->setLabel(kParamTargetLabel);
        param->setHint(kParamTargetHint);
        param->setRange(-DBL_MAX, -DBL_MAX, -DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0., 0., 0., 0., 4., 4., 4., 4.);
        param->setEvaluateOnChange(false);
        param->setIsPersistent(false);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamSetMaster);
        param->setLabel(kParamSetMasterLabel);
        param->setHint(kParamSetMasterHint);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamSetRGB);
        param->setLabel(kParamSetRGBLabel);
        param->setHint(kParamSetRGBHint);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamSetRGBA);
        param->setLabel(kParamSetRGBALabel);
        param->setHint(kParamSetRGBAHint);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamSetA);
        param->setLabel(kParamSetALabel);
        param->setHint(kParamSetAHint);
        if (page) {
            page->addChild(*param);
        }
    }
#ifdef COLORLOOKUP_ADD
    {
        PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamAddCtrlPts);
        param->setLabel(kParamAddCtrlPtsLabel, kParamAddCtrlPtsLabel, kParamAddCtrlPtsLabel);
        if (page) {
            page->addChild(*param);
        }
    }
#endif
#ifdef COLORLOOKUP_RESET
    {
        PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamResetCtrlPts);
        param->setLabel(kParamResetCtrlPtsLabel, kParamResetCtrlPtsLabel, kParamResetCtrlPtsLabel);
        if (page) {
            page->addChild(*param);
        }
    }
#endif
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamMasterCurveMode);
        param->setLabelAndHint(kParamMasterCurveModeLabel);
        assert(param->getNOptions() == eMasterCurveModeStandard);
        param->appendOption(kParamMasterCurveModeOptionStandard);
        assert(param->getNOptions() == eMasterCurveModeWeightedStandard);
        param->appendOption(kParamMasterCurveModeOptionWeightedStandard);
        assert(param->getNOptions() == eMasterCurveModeFilmLike);
        param->appendOption(kParamMasterCurveModeOptionFilmLike);
        assert(param->getNOptions() == eMasterCurveModeLuminance);
        param->appendOption(kParamMasterCurveModeOptionLuminance);
        param->setAnimates(false);
        param->setDefault(kParamMasterCurveModeDefault);
    }
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamLuminanceMath);
        param->setLabel(kParamLuminanceMathLabel);
        param->setHint(kParamLuminanceMathHint);
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
} // ColorLookupPluginFactory::describeInContext

ImageEffect*
ColorLookupPluginFactory::createInstance(OfxImageEffectHandle handle,
                                         ContextEnum /*context*/)
{
    return new ColorLookupPlugin(handle);
}

static ColorLookupPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
