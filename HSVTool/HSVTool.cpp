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
 * OFX HSVTool plugin.
 */

#define _USE_MATH_DEFINES
#include <cmath>
#include <cfloat> // DBL_MAX
#include <algorithm>

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsCoords.h"
#include "ofxsLut.h"
#include "ofxsMacros.h"
#include "ofxsRectangleInteract.h"
#include "ofxsThreadSuite.h"
#include "ofxsMultiThread.h"
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

#define kPluginName "HSVToolOFX"
#define kPluginGrouping "Color"
#define kPluginDescription \
    "Adjust hue, saturation and brightness, or perform color replacement.\n" \
    "\n" \
    "Color replacement:\n" \
    "Set the srcColor and dstColor parameters. The range of the replacement is determined by the three groups of parameters: Hue, Saturation and Brightness.\n" \
    "\n" \
    "Color adjust:\n" \
    "Use the Rotation of the Hue parameter and the Adjustment of the Saturation and Lightness. " \
    "The ranges and falloff parameters allow for more complex adjustments.\n" \
    "\n" \
    "Hue keyer:\n" \
    "Set the outputAlpha parameter (the last one) to All (the default is Hue), and use a viewer to display the Alpha channel. " \
    "First, set the Range parameter of the Hue parameter set and then work down the other Ranges parameters, tuning with the range Falloff and Adjustment parameters." \

#define kPluginIdentifier "net.sf.openfx.HSVToolPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe


#define kGroupColorReplacement "colorReplacement"
#define kGroupColorReplacementLabel "Color Replacement"
#define kGroupColorReplacementHint "Easily replace a given color by another color by setting srcColor and dstColor. Set Src Color first, then Dst Color."
#define kParamSrcColor "srcColor"
#define kParamSrcColorLabel "Src Color"
#define kParamSrcColorHint "Source color for replacement. Changing this parameter sets the hue, saturation and brightness ranges for this color, and sets the fallofs to default values."
#define kParamDstColor "dstColor"
#define kParamDstColorLabel "Dst Color"
#define kParamDstColorHint "Destination color for replacement. Changing this parameter sets the hue rotation, and saturation and brightness adjustments. Should be set after Src Color."

#define kParamEnableRectangle "enableRectangle"
#define kParamEnableRectangleLabel "Src Analysis Rectangle"
#define kParamEnableRectangleHint "Enable the rectangle interact for analysis of Src and Dst colors and ranges."

#define kParamSetSrcFromRectangle "setSrcFromRectangle"
#define kParamSetSrcFromRectangleLabel "Set Src from Rectangle"
#define kParamSetSrcFromRectangleHint "Set the Src color and ranges and the adjustments from the colors of the source image within the selection rectangle and the Dst Color."

#define kGroupHue "hue"
#define kGroupHueLabel "Hue"
#define kGroupHueHint "Hue modification settings."
#define kParamHueRange "hueRange"
#define kParamHueRangeLabel "Hue Range"
#define kParamHueRangeHint "Range of color hues that are modified (in degrees). Red is 0, green is 120, blue is 240. The affected hue range is the smallest interval. For example, if the range is (12, 348), then the selected range is red plus or minus 12 degrees. Exception: if the range width is exactly 360, then all hues are modified."
#define kParamHueRotation "hueRotation"
#define kParamHueRotationLabel "Hue Rotation"
#define kParamHueRotationHint "Rotation of color hues (in degrees) within the range."
#define kParamHueRotationGain "hueRotationGain"
#define kParamHueRotationGainLabel "Hue Rotation Gain"
#define kParamHueRotationGainHint "Factor to be applied to the rotation of color hues (in degrees) within the range. A value of 0 will set all values within range to a constant (computed at the center of the range), and a value of 1 will add hueRotation to all values within range."
#define kParamHueRangeRolloff "hueRangeRolloff"
#define kParamHueRangeRolloffLabel "Hue Range Rolloff"
#define kParamHueRangeRolloffHint "Interval (in degrees) around Hue Range, where hue rotation decreases progressively to zero."

#define kGroupSaturation "saturation"
#define kGroupSaturationLabel "Saturation"
#define kGroupSaturationHint "Saturation modification settings."
#define kParamSaturationRange "saturationRange"
#define kParamSaturationRangeLabel "Saturation Range"
#define kParamSaturationRangeHint "Range of color saturations that are modified."
#define kParamSaturationAdjustment "saturationAdjustment"
#define kParamSaturationAdjustmentLabel "Saturation Adjustment"
#define kParamSaturationAdjustmentHint "Adjustment of color saturations within the range. Saturation is clamped to zero to avoid color inversions."
#define kParamSaturationAdjustmentGain "saturationAdjustmentGain"
#define kParamSaturationAdjustmentGainLabel "Saturation Adjustment Gain"
#define kParamSaturationAdjustmentGainHint "Factor to be applied to the saturation adjustment within the range. A value of 0 will set all values within range to a constant (computed at the center of the range), and a value of 1 will add saturationAdjustment to all values within range."
#define kParamSaturationRangeRolloff "saturationRangeRolloff"
#define kParamSaturationRangeRolloffLabel "Saturation Range Rolloff"
#define kParamSaturationRangeRolloffHint "Interval (in degrees) around Saturation Range, where saturation rotation decreases progressively to zero."

#define kGroupBrightness "brightness"
#define kGroupBrightnessLabel "Brightness"
#define kGroupBrightnessHint "Brightness modification settings."
#define kParamBrightnessRange "brightnessRange"
#define kParamBrightnessRangeLabel "Brightness Range"
#define kParamBrightnessRangeHint "Range of color brightnesss that are modified."
#define kParamBrightnessAdjustment "brightnessAdjustment"
#define kParamBrightnessAdjustmentLabel "Brightness Adjustment"
#define kParamBrightnessAdjustmentHint "Adjustment of color brightnesss within the range."
#define kParamBrightnessAdjustmentGain "brightnessAdjustmentGain"
#define kParamBrightnessAdjustmentGainLabel "Brightness Adjustment Gain"
#define kParamBrightnessAdjustmentGainHint "Factor to be applied to the brightness adjustment within the range. A value of 0 will set all values within range to a constant (computed at the center of the range), and a value of 1 will add brightnessAdjustment to all values within range."
#define kParamBrightnessRangeRolloff "brightnessRangeRolloff"
#define kParamBrightnessRangeRolloffLabel "Brightness Range Rolloff"
#define kParamBrightnessRangeRolloffHint "Interval (in degrees) around Brightness Range, where brightness rotation decreases progressively to zero."

#define kParamClampBlack "clampBlack"
#define kParamClampBlackLabel "Clamp Black"
#define kParamClampBlackHint "All colors below 0 on output are set to 0."

#define kParamClampWhite "clampWhite"
#define kParamClampWhiteLabel "Clamp White"
#define kParamClampWhiteHint "All colors above 1 on output are set to 1."

#define kParamOutputAlpha "outputAlpha"
#define kParamOutputAlphaLabel "Output Alpha"
#define kParamOutputAlphaHint "Output alpha channel. This can either be the source alpha, one of the coefficients for hue, saturation, brightness, or a combination of those. If it is not source alpha, the image on output are unpremultiplied, even if input is premultiplied."
#define kParamOutputAlphaOptionSource "Source", "Alpha channel is kept unmodified.", "source"
#define kParamOutputAlphaOptionHue "Hue", "Set Alpha to the Hue modification mask.", "hue"
#define kParamOutputAlphaOptionSaturation "Saturation", "Set Alpha to the Saturation modification mask.", "saturation"
#define kParamOutputAlphaOptionBrightness "Brightness", "Alpha is set to the Brighness mask.", "brightness"
#define kParamOutputAlphaOptionHueSaturation "min(Hue,Saturation)", "Alpha is set to min(Hue mask,Saturation mask)", "minhuesaturation"
#define kParamOutputAlphaOptionHueBrightness "min(Hue,Brightness)", "Alpha is set to min(Hue mask,Brightness mask)", "minhuebrightness"
#define kParamOutputAlphaOptionSaturationBrightness "min(Saturation,Brightness)", "Alpha is set to min(Saturation mask,Brightness mask)", "minsaturationbrightness"
#define kParamOutputAlphaOptionAll "min(all)", "Alpha is set to min(Hue mask,Saturation mask,Brightness mask)", "min"

enum OutputAlphaEnum
{
    eOutputAlphaSource,
    eOutputAlphaHue,
    eOutputAlphaSaturation,
    eOutputAlphaBrightness,
    eOutputAlphaHueSaturation,
    eOutputAlphaHueBrightness,
    eOutputAlphaSaturationBrightness,
    eOutputAlphaAll,
};

#define kParamPremultChanged "premultChanged"

// Some hosts (e.g. Resolve) may not support normalized defaults (setDefaultCoordinateSystem(eCoordinatesNormalised))
#define kParamDefaultsNormalised "defaultsNormalised"

// to compute the rolloff for a default distribution, we approximate the gaussian with a piecewise linear function
// f(0) = 1, f'(0) = 0
// f(sigma*0.5*sqrt(12)) = 1/2, f'(sigma*0.5*sqrt(12)) = g'(sigma) (g is exp(-x^2/(2*sigma^2)))
// f(inf) = 0, f'(inf) = 0
//#define GAUSSIAN_ROLLOFF 0.8243606354 // exp(1/2)/2
//#define GAUSSIAN_RANGE 1.7320508075 // 0.5*sqrt(12)

// minimum S and V components to take hue into account (hue is too noisy below these values)
#define MIN_SATURATION 0.1
#define MIN_VALUE 0.1

// default fraction of the min-max interval to use as rolloff after rectangle analysis
#define DEFAULT_RECTANGLE_ROLLOFF 0.5

static bool gHostSupportsDefaultCoordinateSystem = true; // for kParamDefaultsNormalised

/* algorithm:
   - convert to HSV
   - compute H, S, and V coefficients: 1 within range, dropping to 0 at range+-rolloff
   - compute min of the three coeffs. coeff = min(hcoeff,scoeff,vcoeff)
   - if global coeff is 0, don't change anything.
   - else, adjust hue by hueRotation*coeff, etc.
   - convert back to RGB

   - when setting srcColor: compute hueRange, satRange, valRange (as empty ranges), set rolloffs to (50,0.3,0.3)
   - when setting dstColor: compute hueRotation, satAdjust and valAdjust
 */
struct HSVToolValues
{
    double hueRange[2];
    double hueRangeWithRolloff[2];
    double hueRotation;
    double hueMean;
    double hueRotationGain;
    double hueRolloff;
    double satRange[2];
    double satAdjust;
    double satAdjustGain;
    double satRolloff;
    double valRange[2];
    double valAdjust;
    double valAdjustGain;
    double valRolloff;
    HSVToolValues()
    {
        hueRange[0] = hueRange[1] = 0.;
        hueRangeWithRolloff[0] = hueRangeWithRolloff[1] = 0.;
        hueRotation = 0.;
        hueMean = 0.;
        hueRotationGain = 1.;
        hueRolloff = 0.;
        satRange[0] = satRange[1] = 0.;
        satAdjust = 0.;
        satAdjustGain = 1.;
        satRolloff = 0.;
        valRange[0] = valRange[1] = 0.;
        valAdjust = 0.;
        valAdjustGain = 1.;
        valRolloff = 0.;
    }
};

//
static inline
double
normalizeAngle(double a)
{
    int c = (int)std::floor(a / 360);

    a -= c * 360;
    assert(a >= 0 && a <= 360);

    return a;
}

static inline
double
normalizeAngleSigned(double a)
{
    return normalizeAngle(a + 180.) - 180.;
}

static inline
bool
angleWithinRange(double h,
                 double h0,
                 double h1)
{
    assert(0 <= h && h <= 360 && 0 <= h0 && h0 <= 360 && 0 <= h1 && h1 <= 360);

    return ( ( h1 < h0 && (h <= h1 || h0 <= h) ) ||
             (h0 <= h && h <= h1) );
}

// Exponentiation by squaring
// works with positive or negative integer exponents
template<typename T>
T
ipow(T base,
     int exp)
{
    T result = T(1);

    if (exp >= 0) {
        while (exp) {
            if (exp & 1) {
                result *= base;
            }
            exp >>= 1;
            base *= base;
        }
    } else {
        exp = -exp;
        while (exp) {
            if (exp & 1) {
                result /= base;
            }
            exp >>= 1;
            base *= base;
        }
    }

    return result;
}

static double
ffloor(double val,
       int decimals)
{
    int p = ipow(10, decimals);

    return std::floor(val * p) / p;
}

static double
fround(double val,
       int decimals)
{
    int p = ipow(10, decimals);

    return std::floor(val * p + 0.5) / p;
}

static double
fceil(double val,
      int decimals)
{
    int p = ipow(10, decimals);

    return std::ceil(val * p) / p;
}

// returns:
// - 0 if outside of [h0, h1]
// - 0 at h0
// - 1 at h1
// - linear from h0 to h1
static inline
double
angleCoeff01(double h,
             double h0,
             double h1)
{
    assert(0 <= h && h <= 360 && 0 <= h0 && h0 <= 360 && 0 <= h1 && h1 <= 360);
    if ( h1 == (h0 + 360.) ) {
        // interval is the whole hue circle
        return 1.;
    }
    if ( !angleWithinRange(h, h0, h1) ) {
        return 0.;
    }
    if (h1 == h0) {
        return 1.;
    }
    if (h1 < h0) {
        h1 += 360;
        if (h < h0) {
            h += 360;
        }
    }
    assert(h0 <= h && h <= h1);

    return (h - h0) / (h1 - h0);
}

// returns:
// - 0 if outside of [h0, h1]
// - 1 at h0
// - 0 at h1
// - linear from h0 to h1
static inline
double
angleCoeff10(double h,
             double h0,
             double h1)
{
    assert(0 <= h && h <= 360 && 0 <= h0 && h0 <= 360 && 0 <= h1 && h1 <= 360);
    if ( !angleWithinRange(h, h0, h1) ) {
        return 0.;
    }
    if (h1 == h0) {
        return 1.;
    }
    if (h1 < h0) {
        h1 += 360;
        if (h < h0) {
            h += 360;
        }
    }
    assert(h0 <= h && h <= h1);

    return (h1 - h) / (h1 - h0);
}

class HSVToolProcessorBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg;
    const Image *_maskImg;
    OutputAlphaEnum _outputAlpha;
    bool _premult;
    int _premultChannel;
    bool _doMasking;
    double _mix;
    bool _maskInvert;

public:

    HSVToolProcessorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _srcImg(NULL)
        , _maskImg(NULL)
        , _outputAlpha(eOutputAlphaSource)
        , _premult(false)
        , _premultChannel(3)
        , _doMasking(false)
        , _mix(1.)
        , _maskInvert(false)
        , _clampBlack(true)
        , _clampWhite(true)
    {
        assert( angleWithinRange(0, 350, 10) );
        assert( angleWithinRange(0, 0, 10) );
        assert( !angleWithinRange(0, 5, 10) );
        assert( !angleWithinRange(0, 10, 350) );
        assert( angleWithinRange(0, 10, 0) );
        assert( angleWithinRange(0, 10, 5) );
        assert(normalizeAngle(-10) == 350);
        assert(normalizeAngle(-370) == 350);
        assert(normalizeAngle(-730) == 350);
        assert(normalizeAngle(370) == 10);
        assert(normalizeAngle(10) == 10);
        assert(normalizeAngle(730) == 10);
    }

    void setSrcImg(const Image *v) {_srcImg = v; }

    void setMaskImg(const Image *v,
                    bool maskInvert) { _maskImg = v; _maskInvert = maskInvert; }

    void doMasking(bool v) {_doMasking = v; }

    void setValues(const HSVToolValues& values,
                   bool clampBlack,
                   bool clampWhite,
                   OutputAlphaEnum outputAlpha,
                   bool premult,
                   int premultChannel,
                   double mix)
    {
        _values = values;
        // set the intervals
        // the hue interval is from the right of h0 to the left of h1
        double h0 = _values.hueRange[0];
        double h1 = _values.hueRange[1];
        if ( h1 == (h0 + 360.) ) {
            // special case: select any hue (useful to rotate all colors)
            _values.hueRange[0] = 0.;
            _values.hueRange[1] = 360.;
            _values.hueRolloff = 0.;
            _values.hueRangeWithRolloff[0] = 0.;
            _values.hueRangeWithRolloff[1] = 360.;
            _values.hueMean = 0.;
        } else {
            h0 = normalizeAngle(h0);
            h1 = normalizeAngle(h1);
            if (h1 < h0) {
                std::swap(h0, h1);
            }
            // take the smallest of both angles
            if ( (h1 - h0) > 180. ) {
                std::swap(h0, h1);
            }
            assert (0 <= h0 && h0 <= 360 && 0 <= h1 && h1 <= 360);
            _values.hueRange[0] = h0;
            _values.hueRange[1] = h1;
            // set strict bounds on rolloff
            if (_values.hueRolloff < 0.) {
                _values.hueRolloff = 0.;
            } else if (_values.hueRolloff >= 180.) {
                _values.hueRolloff = 180.;
            }
            _values.hueRangeWithRolloff[0] = normalizeAngle(h0 - _values.hueRolloff);
            _values.hueRangeWithRolloff[1] = normalizeAngle(h1 + _values.hueRolloff);
            _values.hueMean = normalizeAngle(h0 + normalizeAngleSigned(h1 - h0) / 2);
        }
        if (_values.satRange[1] < _values.satRange[0]) {
            std::swap(_values.satRange[0], _values.satRange[1]);
        }
        if (_values.satRolloff < 0.) {
            _values.satRolloff = 0.;
        }
        if (_values.valRange[1] < _values.valRange[0]) {
            std::swap(_values.valRange[0], _values.valRange[1]);
        }
        if (_values.valRolloff < 0.) {
            _values.valRolloff = 0.;
        }
        _clampBlack = clampBlack;
        _clampWhite = clampWhite;
        _outputAlpha = outputAlpha;
        _premult = premult;
        _premultChannel = premultChannel;
        _mix = mix;
    } // setValues

    void hsvtool(float r,
                 float g,
                 float b,
                 float *hcoeff,
                 float *scoeff,
                 float *vcoeff,
                 float *rout,
                 float *gout,
                 float *bout)
    {
        float h, s, v;

        Color::rgb_to_hsv(r, g, b, &h, &s, &v);

        h *= 360. / OFXS_HUE_CIRCLE;
        const double h0 = _values.hueRange[0];
        const double h1 = _values.hueRange[1];
        const double h0mrolloff = _values.hueRangeWithRolloff[0];
        const double h1prolloff = _values.hueRangeWithRolloff[1];
        // the affected
        if ( angleWithinRange(h, h0, h1) ) {
            *hcoeff = 1.f;
        } else {
            double c0 = 0.;
            double c1 = 0.;
            // check if we are in the rolloff area
            if ( angleWithinRange(h, h0mrolloff, h0) ) {
                c0 = angleCoeff01(h, h0mrolloff, h0);
            }
            if ( angleWithinRange(h, h1, h1prolloff) ) {
                c1 = angleCoeff10(h, h1, h1prolloff);
            }
            *hcoeff = (float)std::max(c0, c1);
        }
        assert(0 <= *hcoeff && *hcoeff <= 1.);
        const double s0 = _values.satRange[0];
        const double s1 = _values.satRange[1];
        const double s0mrolloff = s0 - _values.satRolloff;
        const double s1prolloff = s1 + _values.satRolloff;
        if ( (s0 <= s) && (s <= s1) ) {
            *scoeff = 1.f;
        } else if ( (s0mrolloff <= s) && (s <= s0) ) {
            *scoeff = (float)(s - s0mrolloff) / (float)_values.satRolloff;
        } else if ( (s1 <= s) && (s <= s1prolloff) ) {
            *scoeff = (float)(s1prolloff - s) / (float)_values.satRolloff;
        } else {
            *scoeff = 0.f;
        }
        assert(0 <= *scoeff && *scoeff <= 1.);
        const double v0 = _values.valRange[0];
        const double v1 = _values.valRange[1];
        const double v0mrolloff = v0 - _values.valRolloff;
        const double v1prolloff = v1 + _values.valRolloff;
        if ( (v0 <= v) && (v <= v1) ) {
            *vcoeff = 1.f;
        } else if ( (v0mrolloff <= v) && (v <= v0) ) {
            *vcoeff = (float)(v - v0mrolloff) / (float)_values.valRolloff;
        } else if ( (v1 <= v) && (v <= v1prolloff) ) {
            *vcoeff = (float)(v1prolloff - v) / (float)_values.valRolloff;
        } else {
            *vcoeff = 0.f;
        }
        assert(0 <= *vcoeff && *vcoeff <= 1.);
        float coeff = std::min(std::min(*hcoeff, *scoeff), *vcoeff);
        assert(0 <= coeff && coeff <= 1.);
        if (coeff <= 0.) {
            *rout = r;
            *gout = g;
            *bout = b;
        } else {
            //h += coeff * (float)_values.hueRotation;
            h += coeff * ( (float)_values.hueRotation + (_values.hueRotationGain - 1.) * normalizeAngleSigned(h - _values.hueMean) );
            s += coeff * ( (float)_values.satAdjust + (_values.satAdjustGain - 1.) * (s - (s0 + s1) / 2) );
            if (s < 0) {
                s = 0;
            }
            v += coeff * ( (float)_values.valAdjust + (_values.valAdjustGain - 1.) * (v - (v0 + v1) / 2) );
            h *= OFXS_HUE_CIRCLE / 360.;
            Color::hsv_to_rgb(h, s, v, rout, gout, bout);
        }
        if (_clampBlack) {
            *rout = std::max(0.f, *rout);
            *gout = std::max(0.f, *gout);
            *bout = std::max(0.f, *bout);
        }
        if (_clampWhite) {
            *rout = std::min(1.f, *rout);
            *gout = std::min(1.f, *gout);
            *bout = std::min(1.f, *bout);
        }
    } // hsvtool

private:
    HSVToolValues _values;
    bool _clampBlack;
    bool _clampWhite;
};


template <class PIX, int nComponents, int maxValue>
class HSVToolProcessor
    : public HSVToolProcessorBase
{
public:
    HSVToolProcessor(ImageEffect &instance)
        : HSVToolProcessorBase(instance)
    {
    }

    void multiThreadProcessImages(OfxRectI procWindow)
    {
        assert(nComponents == 3 || nComponents == 4);
        assert(_dstImg);
        float unpPix[4];
        float tmpPix[4];
        // only premultiply output if keeping the source alpha
        const bool premultOut = _premult && (_outputAlpha == eOutputAlphaSource);
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                ofxsUnPremult<PIX, nComponents, maxValue>(srcPix, unpPix, _premult, _premultChannel);
                float hcoeff, scoeff, vcoeff;
                hsvtool(unpPix[0], unpPix[1], unpPix[2], &hcoeff, &scoeff, &vcoeff, &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, premultOut, _premultChannel, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                // if output alpha is not source alpha, set it to the right value
                if ( (nComponents == 4) && (_outputAlpha != eOutputAlphaSource) ) {
                    float a = 0.f;
                    switch (_outputAlpha) {
                    case eOutputAlphaSource:
                        break;
                    case eOutputAlphaHue:
                        a = hcoeff;
                        break;
                    case eOutputAlphaSaturation:
                        a = scoeff;
                        break;
                    case eOutputAlphaBrightness:
                        a = vcoeff;
                        break;
                    case eOutputAlphaHueSaturation:
                        a = std::min(hcoeff, scoeff);
                        break;
                    case eOutputAlphaHueBrightness:
                        a = std::min(hcoeff, vcoeff);
                        break;
                    case eOutputAlphaSaturationBrightness:
                        a = std::min(scoeff, vcoeff);
                        break;
                    case eOutputAlphaAll:
                        a = std::min(std::min(hcoeff, scoeff), vcoeff);
                        break;
                    }
                    if (_doMasking) {
                        // we do, get the pixel from the mask
                        const PIX* maskPix = _maskImg ? (const PIX *)_maskImg->getPixelAddress(x, y) : 0;
                        float maskScale;
                        // figure the scale factor from that pixel
                        if (maskPix == 0) {
                            maskScale = _maskInvert ? 1.f : 0.f;
                        } else {
                            maskScale = *maskPix / float(maxValue);
                            if (_maskInvert) {
                                maskScale = 1.f - maskScale;
                            }
                        }
                        a = std::min(a, maskScale);
                    }
                    dstPix[3] = maxValue * a;
                }

                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    } // multiThreadProcessImages
};

typedef struct HSVColor
{
    HSVColor() : h(0), s(0), v(0) {}

    double h, s, v;
} HSVColor;
typedef struct HSVColorF
{
    HSVColorF() : h(0), s(0), v(0) {}

    float h, s, v;
} HSVColorF;


class HueMeanProcessorBase
    : public ImageProcessor
{
protected:
    Mutex _mutex; //< this is used so we can multi-thread the analysis and protect the shared results
    unsigned long _count;
    double _sumsinh, _sumcosh;

public:
    HueMeanProcessorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _mutex()
        , _count(0)
        , _sumsinh(0.)
        , _sumcosh(0.)
    {
    }

    ~HueMeanProcessorBase()
    {
    }

    double getResult()
    {
        if (_count <= 0) {
            return 0;
        } else {
            double meansinh = _sumsinh / _count;
            double meancosh = _sumcosh / _count;

            // angle mean and sdev from https://en.wikipedia.org/wiki/Directional_statistics#Measures_of_location_and_spread
            return normalizeAngle(std::atan2(meansinh, meancosh) * 180 / M_PI);
            //*huesdev = std::sqrt(std::max(0., -std::log(meansinh*meansinh+meancosh*meancosh)))*180/M_PI;
        }
    }

protected:
    void addResults(double sumsinh,
                    double sumcosh,
                    unsigned long count)
    {
        _mutex.lock();
        _sumsinh += sumsinh;
        _sumcosh += sumcosh;
        _count += count;
        _mutex.unlock();
    }
};

template <class PIX, int nComponents, int maxValue>
class HueMeanProcessor
    : public HueMeanProcessorBase
{
public:
    HueMeanProcessor(ImageEffect &instance)
        : HueMeanProcessorBase(instance)
    {
    }

    ~HueMeanProcessor()
    {
    }

private:

    void pixToHSV(const PIX *p,
                  HSVColorF* hsv)
    {
        if ( (nComponents == 4) || (nComponents == 3) ) {
            float r, g, b;
            r = p[0] / (float)maxValue;
            g = p[1] / (float)maxValue;
            b = p[2] / (float)maxValue;
            Color::rgb_to_hsv(r, g, b, &hsv->h, &hsv->s, &hsv->v);
            hsv->h *= 360 / OFXS_HUE_CIRCLE;
        } else {
            *hsv = HSVColorF();
        }
    }

    void multiThreadProcessImages(OfxRectI procWindow) OVERRIDE FINAL
    {
        double sumsinh = 0.;
        double sumcosh = 0.;
        unsigned long count = 0;

        assert(_dstImg->getBounds().x1 <= procWindow.x1 && procWindow.y2 <= _dstImg->getBounds().y2 &&
               _dstImg->getBounds().y1 <= procWindow.y1 && procWindow.y2 <= _dstImg->getBounds().y2);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            // partial sums to avoid underflows
            double sumsinhLine = 0.;
            double sumcoshLine = 0.;

            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                HSVColorF hsv;
                pixToHSV(dstPix, &hsv);
                if ( (hsv.s > MIN_SATURATION) && (hsv.v > MIN_VALUE) ) {
                    // only take into account pixels that really have a hue
                    sumsinhLine += std::sin(hsv.h * M_PI / 180);
                    sumcoshLine += std::cos(hsv.h * M_PI / 180);
                    ++count;
                }

                dstPix += nComponents;
            }
            sumsinh += sumsinhLine;
            sumcosh += sumcoshLine;
        }

        addResults(sumsinh, sumcosh, count);
    }
};

class HSVRangeProcessorBase
    : public ImageProcessor
{
protected:
    Mutex _mutex; //< this is used so we can multi-thread the analysis and protect the shared results
    float _hmean;

private:
    float _dhmin; // -180..180
    float _dhmax; // -180..180
    float _smin;
    float _smax;
    float _vmin;
    float _vmax;

public:
    HSVRangeProcessorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _mutex()
        , _hmean(0.f)
        , _dhmin(FLT_MAX)
        , _dhmax(-FLT_MAX)
        , _smin(FLT_MAX)
        , _smax(-FLT_MAX)
        , _vmin(FLT_MAX)
        , _vmax(-FLT_MAX)
    {
    }

    ~HSVRangeProcessorBase()
    {
    }

    void setHueMean(float hmean)
    {
        _hmean = hmean;
    }

    void getResults(HSVColor *hsvmin,
                    HSVColor *hsvmax)
    {
        if (_dhmax - _dhmin > 179.9) {
            // more than half circle, take the full circle
            hsvmin->h = 0.;
            hsvmax->h = 360.;
        } else {
            hsvmin->h = normalizeAngle(_hmean + _dhmin);
            hsvmax->h = normalizeAngle(_hmean + _dhmax);
        }
        hsvmin->s = _smin;
        hsvmax->s = _smax;
        hsvmin->v = _vmin;
        hsvmax->v = _vmax;
    }

protected:
    void addResults(const float dhmin,
                    const float dhmax,
                    const float smin,
                    const float smax,
                    const float vmin,
                    const float vmax)
    {
        _mutex.lock();
        if (dhmin < _dhmin) { _dhmin = dhmin; }
        if (dhmax > _dhmax) { _dhmax = dhmax; }
        if (smin < _smin) { _smin = smin; }
        if (smax > _smax) { _smax = smax; }
        if (vmin < _vmin) { _vmin = vmin; }
        if (vmax > _vmax) { _vmax = vmax; }
        _mutex.unlock();
    }
};

template <class PIX, int nComponents, int maxValue>
class HSVRangeProcessor
    : public HSVRangeProcessorBase
{
public:
    HSVRangeProcessor(ImageEffect &instance)
        : HSVRangeProcessorBase(instance)
    {
    }

    ~HSVRangeProcessor()
    {
    }

private:

    void pixToHSV(const PIX *p,
                  HSVColorF* hsv)
    {
        if ( (nComponents == 4) || (nComponents == 3) ) {
            float r, g, b;
            r = p[0] / (float)maxValue;
            g = p[1] / (float)maxValue;
            b = p[2] / (float)maxValue;
            Color::rgb_to_hsv(r, g, b, &hsv->h, &hsv->s, &hsv->v);
            hsv->h *= 360 / OFXS_HUE_CIRCLE;
        } else {
            *hsv = HSVColorF();
        }
    }

    void multiThreadProcessImages(OfxRectI procWindow) OVERRIDE FINAL
    {
        assert(_dstImg->getBounds().x1 <= procWindow.x1 && procWindow.y2 <= _dstImg->getBounds().y2 &&
               _dstImg->getBounds().y1 <= procWindow.y1 && procWindow.y2 <= _dstImg->getBounds().y2);
        float dhmin = 0.;
        float dhmax = 0.;
        float smin = FLT_MAX;
        float smax = -FLT_MAX;
        float vmin = FLT_MAX;
        float vmax = -FLT_MAX;
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                HSVColorF hsv;
                pixToHSV(dstPix, &hsv);
                if ( (hsv.s > MIN_SATURATION) && (hsv.v > MIN_VALUE) ) {
                    float dh = normalizeAngleSigned(hsv.h - _hmean); // relative angle with hmean
                    if (dh < dhmin) { dhmin = dh; }
                    if (dh > dhmax) { dhmax = dh; }
                }
                if (hsv.s < smin) { smin = hsv.s; }
                if (hsv.s > smax) { smax = hsv.s; }
                if (hsv.v < vmin) { vmin = hsv.v; }
                if (hsv.v > vmax) { vmax = hsv.v; }

                dstPix += nComponents;
            }
        }

        addResults(dhmin, dhmax, smin, smax, vmin, vmax);
    }
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class HSVToolPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    HSVToolPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _maskClip(NULL)
        , _srcColor(NULL)
        , _dstColor(NULL)
        , _hueRange(NULL)
        , _hueRotation(NULL)
        , _hueRotationGain(NULL)
        , _hueRangeRolloff(NULL)
        , _saturationRange(NULL)
        , _saturationAdjustment(NULL)
        , _saturationAdjustmentGain(NULL)
        , _saturationRangeRolloff(NULL)
        , _brightnessRange(NULL)
        , _brightnessAdjustment(NULL)
        , _brightnessAdjustmentGain(NULL)
        , _brightnessRangeRolloff(NULL)
        , _clampBlack(NULL)
        , _clampWhite(NULL)
        , _outputAlpha(NULL)
        , _premult(NULL)
        , _premultChannel(NULL)
        , _mix(NULL)
        , _maskApply(NULL)
        , _maskInvert(NULL)
        , _premultChanged(NULL)
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

        _btmLeft = fetchDouble2DParam(kParamRectangleInteractBtmLeft);
        _size = fetchDouble2DParam(kParamRectangleInteractSize);
        _enableRectangle = fetchBooleanParam(kParamEnableRectangle);
        assert(_btmLeft && _size && _enableRectangle);
        _setSrcFromRectangle = fetchPushButtonParam(kParamSetSrcFromRectangle);
        assert(_setSrcFromRectangle);
        _srcColor = fetchRGBParam(kParamSrcColor);
        _dstColor = fetchRGBParam(kParamDstColor);
        _hueRange = fetchDouble2DParam(kParamHueRange);
        _hueRotation = fetchDoubleParam(kParamHueRotation);
        _hueRotationGain = fetchDoubleParam(kParamHueRotationGain);
        _hueRangeRolloff = fetchDoubleParam(kParamHueRangeRolloff);
        _saturationRange = fetchDouble2DParam(kParamSaturationRange);
        _saturationAdjustment = fetchDoubleParam(kParamSaturationAdjustment);
        _saturationAdjustmentGain = fetchDoubleParam(kParamSaturationAdjustmentGain);
        _saturationRangeRolloff = fetchDoubleParam(kParamSaturationRangeRolloff);
        _brightnessRange = fetchDouble2DParam(kParamBrightnessRange);
        _brightnessAdjustment = fetchDoubleParam(kParamBrightnessAdjustment);
        _brightnessAdjustmentGain = fetchDoubleParam(kParamBrightnessAdjustmentGain);
        _brightnessRangeRolloff = fetchDoubleParam(kParamBrightnessRangeRolloff);
        assert(_srcColor && _dstColor &&
               _hueRange && _hueRotation && _hueRotationGain && _hueRangeRolloff &&
               _saturationRange && _saturationAdjustment && _saturationAdjustmentGain && _saturationRangeRolloff &&
               _brightnessRange && _brightnessAdjustment && _brightnessAdjustmentGain && _brightnessRangeRolloff);
        _clampBlack = fetchBooleanParam(kParamClampBlack);
        _clampWhite = fetchBooleanParam(kParamClampWhite);
        assert(_clampBlack && _clampWhite);
        _outputAlpha = fetchChoiceParam(kParamOutputAlpha);
        assert(_outputAlpha);
        _premult = fetchBooleanParam(kParamPremult);
        _premultChannel = fetchChoiceParam(kParamPremultChannel);
        assert(_premult && _premultChannel);
        _mix = fetchDoubleParam(kParamMix);
        _maskApply = ( ofxsMaskIsAlwaysConnected( OFX::getImageEffectHostDescription() ) && paramExists(kParamMaskApply) ) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
        _premultChanged = fetchBooleanParam(kParamPremultChanged);
        assert(_premultChanged);


        // update visibility
        bool enableRectangle = _enableRectangle->getValue();
        _btmLeft->setIsSecretAndDisabled(!enableRectangle);
        _size->setIsSecretAndDisabled(!enableRectangle);
        _setSrcFromRectangle->setIsSecretAndDisabled(!enableRectangle);
        _srcColor->setEnabled(!enableRectangle);

        // honor kParamDefaultsNormalised
        if ( paramExists(kParamDefaultsNormalised) ) {
            // Some hosts (e.g. Resolve) may not support normalized defaults (setDefaultCoordinateSystem(eCoordinatesNormalised))
            // handle these ourselves!
            BooleanParam* param = fetchBooleanParam(kParamDefaultsNormalised);
            assert(param);
            bool normalised = param->getValue();
            if (normalised) {
                OfxPointD size = getProjectExtent();
                OfxPointD origin = getProjectOffset();
                OfxPointD p;
                // we must denormalise all parameters for which setDefaultCoordinateSystem(eCoordinatesNormalised) couldn't be done
                beginEditBlock(kParamDefaultsNormalised);
                p = _btmLeft->getValue();
                _btmLeft->setValue(p.x * size.x + origin.x, p.y * size.y + origin.y);
                p = _size->getValue();
                _size->setValue(p.x * size.x, p.y * size.y);
                param->setValue(false);
                endEditBlock();
            }
        }
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(HSVToolProcessorBase &, const RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    // compute computation window in srcImg
    bool computeWindow(const Image* srcImg, double time, OfxRectI *analysisWindow);

    // update image statistics
    void setSrcFromRectangle(const Image* srcImg, double time, const OfxRectI& analysisWindow);

    void setSrcFromRectangleProcess(HueMeanProcessorBase &huemeanprocessor, HSVRangeProcessorBase &rangeprocessor, const Image* srcImg, double /*time*/, const OfxRectI &analysisWindow, double *hmean, HSVColor *hsvmin, HSVColor *hsvmax);

    template <class PIX, int nComponents, int maxValue>
    void setSrcFromRectangleComponentsDepth(const Image* srcImg,
                                            double time,
                                            const OfxRectI &analysisWindow,
                                            double *hmean,
                                            HSVColor *hsvmin,
                                            HSVColor *hsvmax)
    {
        HueMeanProcessor<PIX, nComponents, maxValue> fred1(*this);
        HSVRangeProcessor<PIX, nComponents, maxValue> fred2(*this);
        setSrcFromRectangleProcess(fred1, fred2, srcImg, time, analysisWindow, hmean, hsvmin, hsvmax);
    }

    template <int nComponents>
    void setSrcFromRectangleComponents(const Image* srcImg,
                                       double time,
                                       const OfxRectI &analysisWindow,
                                       double *hmean,
                                       HSVColor *hsvmin,
                                       HSVColor *hsvmax)
    {
        BitDepthEnum srcBitDepth = srcImg->getPixelDepth();

        switch (srcBitDepth) {
        case eBitDepthUByte: {
            setSrcFromRectangleComponentsDepth<unsigned char, nComponents, 255>(srcImg, time, analysisWindow, hmean, hsvmin, hsvmax);
            break;
        }
        case eBitDepthUShort: {
            setSrcFromRectangleComponentsDepth<unsigned short, nComponents, 65535>(srcImg, time, analysisWindow, hmean, hsvmin, hsvmax);
            break;
        }
        case eBitDepthFloat: {
            setSrcFromRectangleComponentsDepth<float, nComponents, 1>(srcImg, time, analysisWindow, hmean, hsvmin, hsvmax);
            break;
        }
        default:
            throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    Clip *_maskClip;
    Double2DParam* _btmLeft;
    Double2DParam* _size;
    BooleanParam* _enableRectangle;
    PushButtonParam* _setSrcFromRectangle;
    RGBParam *_srcColor;
    RGBParam *_dstColor;
    Double2DParam *_hueRange;
    DoubleParam *_hueRotation;
    DoubleParam *_hueRotationGain;
    DoubleParam *_hueRangeRolloff;
    Double2DParam *_saturationRange;
    DoubleParam *_saturationAdjustment;
    DoubleParam *_saturationAdjustmentGain;
    DoubleParam *_saturationRangeRolloff;
    Double2DParam *_brightnessRange;
    DoubleParam *_brightnessAdjustment;
    DoubleParam *_brightnessAdjustmentGain;
    DoubleParam *_brightnessRangeRolloff;
    BooleanParam *_clampBlack;
    BooleanParam *_clampWhite;
    ChoiceParam *_outputAlpha;
    BooleanParam *_premult;
    ChoiceParam *_premultChannel;
    DoubleParam *_mix;
    BooleanParam *_maskApply;
    BooleanParam *_maskInvert;
    BooleanParam* _premultChanged; // set to true the first time the user connects src
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
HSVToolPlugin::setupAndProcess(HSVToolProcessorBase &processor,
                               const RenderArguments &args)
{
    const double time = args.time;

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

    OutputAlphaEnum outputAlpha = (OutputAlphaEnum)_outputAlpha->getValueAtTime(time);
    if (outputAlpha != eOutputAlphaSource) {
        if (dstComponents != ePixelComponentRGBA) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host dit not take into account output components");
            throwSuiteStatusException(kOfxStatErrImageFormat);

            return;
        }
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
        // set the components of _dstClip
        if ( (srcBitDepth != dstBitDepth) || ( (outputAlpha == eOutputAlphaSource) && (srcComponents != dstComponents) ) ) {
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

    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );
    processor.setRenderWindow(args.renderWindow);

    HSVToolValues values;
    _hueRange->getValueAtTime(time, values.hueRange[0], values.hueRange[1]);
    values.hueRangeWithRolloff[0] = values.hueRangeWithRolloff[1] = 0; // set in setValues()
    values.hueRotation = _hueRotation->getValueAtTime(time);
    values.hueRotationGain = _hueRotationGain->getValueAtTime(time);
    values.hueMean = 0; // set in setValues()
    values.hueRolloff = _hueRangeRolloff->getValueAtTime(time);
    _saturationRange->getValueAtTime(time, values.satRange[0], values.satRange[1]);
    values.satAdjust = _saturationAdjustment->getValueAtTime(time);
    values.satAdjustGain = _saturationAdjustmentGain->getValueAtTime(time);
    values.satRolloff = _saturationRangeRolloff->getValueAtTime(time);
    _brightnessRange->getValueAtTime(time, values.valRange[0], values.valRange[1]);
    values.valAdjust = _brightnessAdjustment->getValueAtTime(time);
    values.valAdjustGain = _brightnessAdjustmentGain->getValueAtTime(time);
    values.valRolloff = _brightnessRangeRolloff->getValueAtTime(time);

    bool clampBlack, clampWhite;
    _clampBlack->getValueAtTime(time, clampBlack);
    _clampWhite->getValueAtTime(time, clampWhite);

    bool premult;
    int premultChannel;
    _premult->getValueAtTime(time, premult);
    _premultChannel->getValueAtTime(time, premultChannel);
    double mix;
    _mix->getValueAtTime(time, mix);

    processor.setValues(values, clampBlack, clampWhite, outputAlpha, premult, premultChannel, mix);
    processor.process();
} // HSVToolPlugin::setupAndProcess

// the overridden render function
void
HSVToolPlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(dstComponents == ePixelComponentRGB || dstComponents == ePixelComponentRGBA);
    if (dstComponents == ePixelComponentRGBA) {
        switch (dstBitDepth) {
        case eBitDepthUByte: {
            HSVToolProcessor<unsigned char, 4, 255> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthUShort: {
            HSVToolProcessor<unsigned short, 4, 65535> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthFloat: {
            HSVToolProcessor<float, 4, 1> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        default:
            throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else {
        assert(dstComponents == ePixelComponentRGB);
        switch (dstBitDepth) {
        case eBitDepthUByte: {
            HSVToolProcessor<unsigned char, 3, 255> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthUShort: {
            HSVToolProcessor<unsigned short, 3, 65535> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthFloat: {
            HSVToolProcessor<float, 3, 1> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        default:
            throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
} // HSVToolPlugin::render

bool
HSVToolPlugin::isIdentity(const IsIdentityArguments &args,
                          Clip * &identityClip,
                          double & /*identityTime*/
                          , int& /*view*/, std::string& /*plane*/)
{
    if (!_srcClip || !_srcClip->isConnected()) {
        return false;
    }
    const double time = args.time;
    double mix = _mix->getValueAtTime(time);

    if (mix == 0.) {
        identityClip = _srcClip;

        return true;
    }


    if (_srcClip->getPixelComponents() == ePixelComponentRGBA) {
        // check cases where alpha is affected, even if colors don't change
        OutputAlphaEnum outputAlpha = (OutputAlphaEnum)_outputAlpha->getValueAtTime(time);
        if (outputAlpha != eOutputAlphaSource) {
            double hueMin, hueMax;
            _hueRange->getValueAtTime(time, hueMin, hueMax);
            bool alphaHue = (hueMin != 0. || hueMax != 360.);
            double satMin, satMax;
            _saturationRange->getValueAtTime(time, satMin, satMax);
            bool alphaSat = (satMin != 0. || satMax != 1.);
            double valMin, valMax;
            _brightnessRange->getValueAtTime(time, valMin, valMax);
            bool alphaVal = (valMin != 0. || valMax != 1.);
            switch (outputAlpha) {
            // coverity[dead_error_begin]
            case eOutputAlphaSource:
                break;
            case eOutputAlphaHue:
                if (alphaHue) {
                    return false;
                }
                break;
            case eOutputAlphaSaturation:
                if (alphaSat) {
                    return false;
                }
                break;
            case eOutputAlphaBrightness:
                if (alphaVal) {
                    return false;
                }
                break;
            case eOutputAlphaHueSaturation:
                if (alphaHue || alphaSat) {
                    return false;
                }
                break;
            case eOutputAlphaHueBrightness:
                if (alphaHue || alphaVal) {
                    return false;
                }
                break;
            case eOutputAlphaSaturationBrightness:
                if (alphaSat || alphaVal) {
                    return false;
                }
                break;
            case eOutputAlphaAll:
                if (alphaHue || alphaSat || alphaVal) {
                    return false;
                }
                break;
            }
        }
    }

    // isIdentity=true if hueRotation, satAdjust and valAdjust = 0.
    double hueRotation;
    _hueRotation->getValueAtTime(time, hueRotation);
    double saturationAdjustment;
    _saturationAdjustment->getValueAtTime(time, saturationAdjustment);
    double brightnessAdjustment;
    _brightnessAdjustment->getValueAtTime(time, brightnessAdjustment);
    if ( (hueRotation == 0.) && (saturationAdjustment == 0.) && (brightnessAdjustment == 0.) ) {
        identityClip = _srcClip;

        return true;
    }

    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        if (!maskInvert) {
            OfxRectI maskRoD;
            Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
            // effect is identity if the renderWindow doesn't intersect the mask RoD
            if ( !Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0) ) {
                identityClip = _srcClip;

                return true;
            }
        }
    }

    return false;
} // HSVToolPlugin::isIdentity

bool
HSVToolPlugin::computeWindow(const Image* srcImg,
                             double time,
                             OfxRectI *analysisWindow)
{
    OfxRectD regionOfInterest;
    bool enableRectangle = _enableRectangle->getValueAtTime(time);

    if (!enableRectangle && _srcClip) {
        return false; // no analysis in this case
        /*
           // use the src region of definition as rectangle, but avoid infinite rectangle
           regionOfInterest = _srcClip->getRegionOfDefinition(time);
           OfxPointD size = getProjectSize();
           OfxPointD offset = getProjectOffset();
           if (regionOfInterest.x1 <= kOfxFlagInfiniteMin) {
            regionOfInterest.x1 = offset.x;
           }
           if (regionOfInterest.x2 >= kOfxFlagInfiniteMax) {
            regionOfInterest.x2 = offset.x + size.x;
           }
           if (regionOfInterest.y1 <= kOfxFlagInfiniteMin) {
            regionOfInterest.y1 = offset.y;
           }
           if (regionOfInterest.y2 >= kOfxFlagInfiniteMax) {
            regionOfInterest.y2 = offset.y + size.y;
           }
         */
    } else {
        _btmLeft->getValueAtTime(time, regionOfInterest.x1, regionOfInterest.y1);
        _size->getValueAtTime(time, regionOfInterest.x2, regionOfInterest.y2);
        regionOfInterest.x2 += regionOfInterest.x1;
        regionOfInterest.y2 += regionOfInterest.y1;
    }
    Coords::toPixelEnclosing(regionOfInterest,
                             srcImg->getRenderScale(),
                             srcImg->getPixelAspectRatio(),
                             analysisWindow);

    return Coords::rectIntersection(*analysisWindow, srcImg->getBounds(), analysisWindow);
}

void
HSVToolPlugin::setSrcFromRectangle(const Image* srcImg,
                                   double time,
                                   const OfxRectI &analysisWindow)
{
    double hmean = 0.;
    HSVColor hsvmin, hsvmax;
    PixelComponentEnum srcComponents  = srcImg->getPixelComponents();

    assert(srcComponents == ePixelComponentAlpha || srcComponents == ePixelComponentRGB || srcComponents == ePixelComponentRGBA);
    if (srcComponents == ePixelComponentAlpha) {
        setSrcFromRectangleComponents<1>(srcImg, time, analysisWindow, &hmean, &hsvmin, &hsvmax);
    } else if (srcComponents == ePixelComponentRGBA) {
        setSrcFromRectangleComponents<4>(srcImg, time, analysisWindow, &hmean, &hsvmin, &hsvmax);
    } else if (srcComponents == ePixelComponentRGB) {
        setSrcFromRectangleComponents<3>(srcImg, time, analysisWindow, &hmean, &hsvmin, &hsvmax);
    } else {
        // coverity[dead_error_line]
        throwSuiteStatusException(kOfxStatErrUnsupported);

        return;
    }

    if ( abort() ) {
        return;
    }

    float h = normalizeAngle(hmean);
    float s = (hsvmin.s + hsvmax.s) / 2;
    float v = (hsvmin.v + hsvmax.v) / 2;
    float r = 0.f;
    float g = 0.f;
    float b = 0.f;
    Color::hsv_to_rgb(h * OFXS_HUE_CIRCLE / 360., s, v, &r, &g, &b);
    double tor, tog, tob;
    _dstColor->getValueAtTime(time, tor, tog, tob);
    float toh, tos, tov;
    Color::rgb_to_hsv( (float)tor, (float)tog, (float)tob, &toh, &tos, &tov );
    double dh = normalizeAngleSigned(toh * 360. / OFXS_HUE_CIRCLE - h);
    // range is from mean+sdev*(GAUSSIAN_RANGE-GAUSSIAN_ROLLOFF) to mean+sdev*(GAUSSIAN_RANGE+GAUSSIAN_ROLLOFF)
    beginEditBlock("setSrcFromRectangle");
    _srcColor->setValue( fround(r, 4), fround(g, 4), fround(b, 4) );
    _hueRange->setValue( ffloor(hsvmin.h, 2), fceil(hsvmax.h, 2) );
    double hrange = hsvmax.h - hsvmin.h;
    if (hrange < 0) {
        hrange += 360.;
    }
    double hrolloff = std::min(hrange * DEFAULT_RECTANGLE_ROLLOFF, (360 - hrange) / 2);
    _hueRangeRolloff->setValue( ffloor(hrolloff, 2) );
    if (tov != 0.) { // no need to rotate if target color is black
        _hueRotation->setValue( fround(dh, 2) );
    }
    _saturationRange->setValue( ffloor(hsvmin.s, 4), fceil(hsvmax.s, 4) );
    _saturationRangeRolloff->setValue( ffloor( (hsvmax.s - hsvmin.s) * DEFAULT_RECTANGLE_ROLLOFF, 4 ) );
    if (tov != 0.) { // no need to adjust saturation if target color is black
        _saturationAdjustment->setValue( fround(tos - s, 4) );
    }
    _brightnessRange->setValue( ffloor(hsvmin.v, 4), fceil(hsvmax.v, 4) );
    _brightnessRangeRolloff->setValue( ffloor( (hsvmax.v - hsvmin.v) * DEFAULT_RECTANGLE_ROLLOFF, 4 ) );
    _brightnessAdjustment->setValue( fround(tov - v, 4) );
    endEditBlock();
} // HSVToolPlugin::setSrcFromRectangle

/* set up and run a processor */
void
HSVToolPlugin::setSrcFromRectangleProcess(HueMeanProcessorBase &huemeanprocessor,
                                          HSVRangeProcessorBase &hsvrangeprocessor,
                                          const Image* srcImg,
                                          double /*time*/,
                                          const OfxRectI &analysisWindow,
                                          double *hmean,
                                          HSVColor *hsvmin,
                                          HSVColor *hsvmax)
{
    // set the images
    huemeanprocessor.setDstImg( const_cast<Image*>(srcImg) ); // not a bug: we only set dst

    // set the render window
    huemeanprocessor.setRenderWindow(analysisWindow);

    // Call the base class process member, this will call the derived templated process code
    huemeanprocessor.process();

    if ( abort() ) {
        return;
    }

    *hmean = huemeanprocessor.getResult();

    // set the images
    hsvrangeprocessor.setDstImg( const_cast<Image*>(srcImg) ); // not a bug: we only set dst

    // set the render window
    hsvrangeprocessor.setRenderWindow(analysisWindow);
    hsvrangeprocessor.setHueMean(*hmean);


    // Call the base class process member, this will call the derived templated process code
    hsvrangeprocessor.process();

    if ( abort() ) {
        return;
    }
    hsvrangeprocessor.getResults(hsvmin, hsvmax);
}

void
HSVToolPlugin::changedParam(const InstanceChangedArgs &args,
                            const std::string &paramName)
{
    const double time = args.time;

    if ( (paramName == kParamSrcColor) && (args.reason == eChangeUserEdit) ) {
        // - when setting srcColor: compute hueRange, satRange, valRange (as empty ranges), set rolloffs to (50,0.3,0.3)
        double r, g, b;
        _srcColor->getValueAtTime(time, r, g, b);
        float h, s, v;
        Color::rgb_to_hsv( (float)r, (float)g, (float)b, &h, &s, &v );
        h *= 360. / OFXS_HUE_CIRCLE;
        double tor, tog, tob;
        _dstColor->getValueAtTime(time, tor, tog, tob);
        float toh, tos, tov;
        Color::rgb_to_hsv( (float)tor, (float)tog, (float)tob, &toh, &tos, &tov );
        toh *= 360. / OFXS_HUE_CIRCLE;
        double dh = normalizeAngleSigned(toh - h);
        beginEditBlock("setSrc");
        _hueRange->setValue(h, h);
        _hueRangeRolloff->setValue(50.);
        if (tov != 0.) { // no need to rotate if target color is black
            _hueRotation->setValue(dh);
        }
        _saturationRange->setValue(s, s);
        _saturationRangeRolloff->setValue(0.3);
        if (tov != 0.) { // no need to adjust saturation if target color is black
            _saturationAdjustment->setValue(tos - s);
        }
        _brightnessRange->setValue(v, v);
        _brightnessRangeRolloff->setValue(0.3);
        _brightnessAdjustment->setValue(tov - v);
        endEditBlock();
    } else if (paramName == kParamEnableRectangle) {
        // update visibility
        bool enableRectangle = _enableRectangle->getValueAtTime(time);
        _btmLeft->setIsSecretAndDisabled(!enableRectangle);
        _size->setIsSecretAndDisabled(!enableRectangle);
        _setSrcFromRectangle->setIsSecretAndDisabled(!enableRectangle);
        _srcColor->setEnabled(!enableRectangle);
    } else if ( (paramName == kParamSetSrcFromRectangle) && (args.reason == eChangeUserEdit) ) {
        auto_ptr<Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                  _srcClip->fetchImage(args.time) : 0 );
        if ( src.get() ) {
            if ( (src->getRenderScale().x != args.renderScale.x) ||
                 ( src->getRenderScale().y != args.renderScale.y) ) {
                setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                throwSuiteStatusException(kOfxStatFailed);
            }
            OfxRectI analysisWindow;
            bool intersect = computeWindow(src.get(), args.time, &analysisWindow);
            if (intersect) {
#             ifdef kOfxImageEffectPropInAnalysis // removed from OFX 1.4
                getPropertySet().propSetInt(kOfxImageEffectPropInAnalysis, 1, false);
#             endif
                setSrcFromRectangle(src.get(), args.time, analysisWindow);
#             ifdef kOfxImageEffectPropInAnalysis // removed from OFX 1.4
                getPropertySet().propSetInt(kOfxImageEffectPropInAnalysis, 0, false);
#             endif
            }
        }
    } else if ( (paramName == kParamDstColor) && (args.reason == eChangeUserEdit) ) {
        // - when setting dstColor: compute hueRotation, satAdjust and valAdjust
        double r, g, b;
        _srcColor->getValueAtTime(time, r, g, b);
        float h, s, v;
        Color::rgb_to_hsv( (float)r, (float)g, (float)b, &h, &s, &v );
        h *= 360. / OFXS_HUE_CIRCLE;
        double tor, tog, tob;
        _dstColor->getValueAtTime(time, tor, tog, tob);
        float toh, tos, tov;
        Color::rgb_to_hsv( (float)tor, (float)tog, (float)tob, &toh, &tos, &tov );
        toh *= 360. / OFXS_HUE_CIRCLE;
        double dh = normalizeAngleSigned(toh - h);
        beginEditBlock("setDst");
        if (tov != 0.) { // no need to adjust hue or saturation if target color is black
            _hueRotation->setValue(dh);
            _saturationAdjustment->setValue(tos - s);
        }
        _brightnessAdjustment->setValue(tov - v);
        endEditBlock();
    } else if ( (paramName == kParamPremult) && (args.reason == eChangeUserEdit) ) {
        _premultChanged->setValue(true);
    }
} // HSVToolPlugin::changedParam

void
HSVToolPlugin::changedClip(const InstanceChangedArgs &args,
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

/* Override the clip preferences */
void
HSVToolPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    // set the components of _dstClip
    OutputAlphaEnum outputAlpha = (OutputAlphaEnum)_outputAlpha->getValue();

    if (outputAlpha != eOutputAlphaSource) {
        // output must be RGBA, output image is unpremult
        clipPreferences.setClipComponents(*_dstClip, ePixelComponentRGBA);
        clipPreferences.setClipComponents(*_srcClip, ePixelComponentRGBA);
        clipPreferences.setOutputPremultiplication(eImageUnPreMultiplied);
    }
}

class HSVToolInteract
    : public RectangleInteract
{
public:

    HSVToolInteract(OfxInteractHandle handle,
                    ImageEffect* effect)
        : RectangleInteract(handle, effect)
        , _enableRectangle(NULL)
    {
        _enableRectangle = effect->fetchBooleanParam(kParamEnableRectangle);
        addParamToSlaveTo(_enableRectangle);
    }

private:

    // overridden functions from Interact to do things
    virtual bool draw(const DrawArgs &args) OVERRIDE FINAL
    {
        bool enableRectangle = _enableRectangle->getValueAtTime(args.time);

        if (enableRectangle) {
            return RectangleInteract::draw(args);
        }

        return false;
    }

    virtual bool penMotion(const PenArgs &args) OVERRIDE FINAL
    {
        bool enableRectangle = _enableRectangle->getValueAtTime(args.time);

        if (enableRectangle) {
            return RectangleInteract::penMotion(args);
        }

        return false;
    }

    virtual bool penDown(const PenArgs &args) OVERRIDE FINAL
    {
        bool enableRectangle = _enableRectangle->getValueAtTime(args.time);

        if (enableRectangle) {
            return RectangleInteract::penDown(args);
        }

        return false;
    }

    virtual bool penUp(const PenArgs &args) OVERRIDE FINAL
    {
        bool enableRectangle = _enableRectangle->getValueAtTime(args.time);

        if (enableRectangle) {
            return RectangleInteract::penUp(args);
        }

        return false;
    }

    //virtual bool keyDown(const KeyArgs &args) OVERRIDE;
    //virtual bool keyUp(const KeyArgs & args) OVERRIDE;
    //virtual void loseFocus(const FocusArgs &args) OVERRIDE FINAL;


    BooleanParam* _enableRectangle;
};

class HSVToolOverlayDescriptor
    : public DefaultEffectOverlayDescriptor<HSVToolOverlayDescriptor, HSVToolInteract>
{
};

mDeclarePluginFactory(HSVToolPluginFactory, {ofxsThreadSuiteCheck();}, {});

void
HSVToolPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextPaint);
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
    desc.setOverlayInteractDescriptor(new HSVToolOverlayDescriptor);
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentRGBA);
#endif
}

void
HSVToolPluginFactory::describeInContext(ImageEffectDescriptor &desc,
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

    {
        GroupParamDescriptor *group = desc.defineGroupParam(kGroupColorReplacement);
        if (group) {
            group->setLabel(kGroupColorReplacementLabel);
            group->setHint(kGroupColorReplacementHint);
            group->setEnabled(true);
            if (page) {
                page->addChild(*group);
            }
        }

        // enableRectangle
        {
            BooleanParamDescriptor *param = desc.defineBooleanParam(kParamEnableRectangle);
            param->setLabel(kParamEnableRectangleLabel);
            param->setHint(kParamEnableRectangleHint);
            param->setDefault(false);
            param->setAnimates(false);
            param->setEvaluateOnChange(false);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // btmLeft
        {
            Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamRectangleInteractBtmLeft);
            param->setLabel(kParamRectangleInteractBtmLeftLabel);
            param->setDoubleType(eDoubleTypeXYAbsolute);
            if ( param->supportsDefaultCoordinateSystem() ) {
                param->setDefaultCoordinateSystem(eCoordinatesNormalised); // no need of kParamDefaultsNormalised
            } else {
                gHostSupportsDefaultCoordinateSystem = false; // no multithread here, see kParamDefaultsNormalised
            }
            param->setDefault(0.25, 0.25);
            param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0, 0, 10000, 10000); // Resolve requires display range or values are clamped to (-1,1)
            param->setIncrement(1.);
            param->setHint(kParamRectangleInteractBtmLeftHint);
            param->setDigits(0);
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // size
        {
            Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamRectangleInteractSize);
            param->setLabel(kParamRectangleInteractSizeLabel);
            param->setDoubleType(eDoubleTypeXY);
            if ( param->supportsDefaultCoordinateSystem() ) {
                param->setDefaultCoordinateSystem(eCoordinatesNormalised); // no need of kParamDefaultsNormalised
            } else {
                gHostSupportsDefaultCoordinateSystem = false; // no multithread here, see kParamDefaultsNormalised
            }
            param->setDefault(0.5, 0.5);
            param->setRange(0., 0., DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0, 0, 10000, 10000); // Resolve requires display range or values are clamped to (-1,1)
            param->setIncrement(1.);
            param->setDimensionLabels(kParamRectangleInteractSizeDim1, kParamRectangleInteractSizeDim2);
            param->setHint(kParamRectangleInteractSizeHint);
            param->setDigits(0);
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamSetSrcFromRectangle);
            param->setLabel(kParamSetSrcFromRectangleLabel);
            param->setHint(kParamSetSrcFromRectangleHint);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            RGBParamDescriptor *param = desc.defineRGBParam(kParamSrcColor);
            param->setLabel(kParamSrcColorLabel);
            param->setHint(kParamSrcColorHint);
            param->setEvaluateOnChange(false);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            RGBParamDescriptor *param = desc.defineRGBParam(kParamDstColor);
            param->setLabel(kParamDstColorLabel);
            param->setHint(kParamDstColorHint);
            param->setEvaluateOnChange(false);
            param->setLayoutHint(eLayoutHintDivider); // last parameter in the group
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
    }

    {
        GroupParamDescriptor *group = desc.defineGroupParam(kGroupHue);
        if (group) {
            group->setLabel(kGroupHueLabel);
            group->setHint(kGroupHueHint);
            group->setEnabled(true);
            if (page) {
                page->addChild(*group);
            }
        }

        {
            Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamHueRange);
            param->setLabel(kParamHueRangeLabel);
            param->setHint(kParamHueRangeHint);
            param->setDimensionLabels("", ""); // the two values have the same meaning (they just define a range)
            param->setDefault(0., 360.);
            param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0., 0., 360., 360.);
            param->setDoubleType(eDoubleTypeAngle);
            param->setUseHostNativeOverlayHandle(false);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamHueRotation);
            param->setLabel(kParamHueRotationLabel);
            param->setHint(kParamHueRotationHint);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-180., 180.);
            param->setDoubleType(eDoubleTypeAngle);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamHueRotationGain);
            param->setLabel(kParamHueRotationGainLabel);
            param->setHint(kParamHueRotationGainHint);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0., 2.);
            param->setDefault(1.);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamHueRangeRolloff);
            param->setLabel(kParamHueRangeRolloffLabel);
            param->setHint(kParamHueRangeRolloffHint);
            param->setRange(0., 180.);
            param->setDisplayRange(0., 180.);
            param->setDoubleType(eDoubleTypeAngle);
            param->setLayoutHint(eLayoutHintDivider); // last parameter in the group
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
    }

    {
        GroupParamDescriptor *group = desc.defineGroupParam(kGroupSaturation);
        if (group) {
            group->setLabel(kGroupSaturationLabel);
            group->setHint(kGroupSaturationHint);
            group->setEnabled(true);
            if (page) {
                page->addChild(*group);
            }
        }

        {
            Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamSaturationRange);
            param->setLabel(kParamSaturationRangeLabel);
            param->setHint(kParamSaturationRangeHint);
            param->setDimensionLabels("", ""); // the two values have the same meaning (they just define a range)
            param->setDefault(0., 1.);
            param->setDoubleType(eDoubleTypePlain);
            param->setRange(0., 0., 1., 1.);
            param->setDisplayRange(0., 0., 1, 1);
            param->setUseHostNativeOverlayHandle(false);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSaturationAdjustment);
            param->setLabel(kParamSaturationAdjustmentLabel);
            param->setHint(kParamSaturationAdjustmentHint);
            param->setRange(-1., 1.);
            param->setDisplayRange(0., 1.);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSaturationAdjustmentGain);
            param->setLabel(kParamSaturationAdjustmentGainLabel);
            param->setHint(kParamSaturationAdjustmentGainHint);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0., 2.);
            param->setDefault(1.);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSaturationRangeRolloff);
            param->setLabel(kParamSaturationRangeRolloffLabel);
            param->setHint(kParamSaturationRangeRolloffHint);
            param->setRange(0., 1.);
            param->setDisplayRange(0., 1.);
            param->setLayoutHint(eLayoutHintDivider); // last parameter in the group
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
    }

    {
        GroupParamDescriptor *group = desc.defineGroupParam(kGroupBrightness);
        if (group) {
            group->setLabel(kGroupBrightnessLabel);
            group->setHint(kGroupBrightnessHint);
            group->setEnabled(true);
            if (page) {
                page->addChild(*group);
            }
        }

        {
            Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamBrightnessRange);
            param->setLabel(kParamBrightnessRangeLabel);
            param->setHint(kParamBrightnessRangeHint);
            param->setDimensionLabels("", ""); // the two values have the same meaning (they just define a range)
            param->setDefault(0., 1.);
            param->setDoubleType(eDoubleTypePlain);
            param->setRange(0., 0., DBL_MAX, DBL_MAX);
            param->setDisplayRange(0., 0., 1, 1);
            param->setUseHostNativeOverlayHandle(false);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamBrightnessAdjustment);
            param->setLabel(kParamBrightnessAdjustmentLabel);
            param->setHint(kParamBrightnessAdjustmentHint);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0., 1.);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamBrightnessAdjustmentGain);
            param->setLabel(kParamBrightnessAdjustmentGainLabel);
            param->setHint(kParamBrightnessAdjustmentGainHint);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0., 2.);
            param->setDefault(1.);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamBrightnessRangeRolloff);
            param->setLabel(kParamBrightnessRangeRolloffLabel);
            param->setHint(kParamBrightnessRangeRolloffHint);
            param->setRange(0., DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0., 1.);
            param->setLayoutHint(eLayoutHintDivider); // last parameter in the group
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
    }

    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamClampBlack);
        param->setLabel(kParamClampBlackLabel);
        param->setHint(kParamClampBlackHint);
        param->setDefault(true);
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

    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamOutputAlpha);
        param->setLabel(kParamOutputAlphaLabel);
        param->setHint(kParamOutputAlphaHint);
        assert(param->getNOptions() == (int)eOutputAlphaSource);
        param->appendOption(kParamOutputAlphaOptionSource);
        assert(param->getNOptions() == (int)eOutputAlphaHue);
        param->appendOption(kParamOutputAlphaOptionHue);
        assert(param->getNOptions() == (int)eOutputAlphaSaturation);
        param->appendOption(kParamOutputAlphaOptionSaturation);
        assert(param->getNOptions() == (int)eOutputAlphaBrightness);
        param->appendOption(kParamOutputAlphaOptionBrightness);
        assert(param->getNOptions() == (int)eOutputAlphaHueSaturation);
        param->appendOption(kParamOutputAlphaOptionHueSaturation);
        assert(param->getNOptions() == (int)eOutputAlphaHueBrightness);
        param->appendOption(kParamOutputAlphaOptionHueBrightness);
        assert(param->getNOptions() == (int)eOutputAlphaSaturationBrightness);
        param->appendOption(kParamOutputAlphaOptionSaturationBrightness);
        assert(param->getNOptions() == (int)eOutputAlphaAll);
        param->appendOption(kParamOutputAlphaOptionAll);
        param->setDefault( (int)eOutputAlphaHue );
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
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

    // Some hosts (e.g. Resolve) may not support normalized defaults (setDefaultCoordinateSystem(eCoordinatesNormalised))
    if (!gHostSupportsDefaultCoordinateSystem) {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamDefaultsNormalised);
        param->setDefault(true);
        param->setEvaluateOnChange(false);
        param->setIsSecretAndDisabled(true);
        param->setIsPersistent(true);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
} // HSVToolPluginFactory::describeInContext

ImageEffect*
HSVToolPluginFactory::createInstance(OfxImageEffectHandle handle,
                                     ContextEnum /*context*/)
{
    return new HSVToolPlugin(handle);
}

static HSVToolPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
