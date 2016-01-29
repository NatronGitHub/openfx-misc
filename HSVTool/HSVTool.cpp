/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2015 INRIA
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

#include <cmath>
#include <algorithm>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsCoords.h"
#include "ofxsLut.h"
#include "ofxsMacros.h"

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

#define kGroupHue "hue"
#define kGroupHueLabel "Hue"
#define kGroupHueHint "Hue modification settings."
#define kParamHueRange "hueRange"
#define kParamHueRangeLabel "Hue Range"
#define kParamHueRangeHint "Range of color hues that are modified (in degrees). Red is 0, green is 120, blue is 240. The affected hue range is the smallest interval. For example, if the range is (12, 348), then the selected range is red plus or minus 12 degrees. Exception: if the range width is exactly 360, then all hues are modified."
#define kParamHueRotation "hueRotation"
#define kParamHueRotationLabel "Hue Rotation"
#define kParamHueRotationHint "Rotation of color hues (in degrees) within the range."
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
#define kParamOutputAlphaOptionSource "Source"
#define kParamOutputAlphaOptionSourceHint "Alpha channel is kept unmodified"
#define kParamOutputAlphaOptionHue "Hue"
#define kParamOutputAlphaOptionHueHint "Set Alpha to the Hue modification mask"
#define kParamOutputAlphaOptionSaturation "Saturation"
#define kParamOutputAlphaOptionSaturationHint "Set Alpha to the Saturation modification mask"
#define kParamOutputAlphaOptionBrightness "Brightness"
#define kParamOutputAlphaOptionBrightnessHint "Alpha is set to the Brighness mask"
#define kParamOutputAlphaOptionHueSaturation "min(Hue,Saturation)"
#define kParamOutputAlphaOptionHueSaturationHint "Alpha is set to min(Hue mask,Saturation mask)"
#define kParamOutputAlphaOptionHueBrightness "min(Hue,Brightness)"
#define kParamOutputAlphaOptionHueBrightnessHint "Alpha is set to min(Hue mask,Brightness mask)"
#define kParamOutputAlphaOptionSaturationBrightness "min(Saturation)"
#define kParamOutputAlphaOptionSaturationBrightnessHint "Alpha is set to min(Hue mask,Saturation mask)"
#define kParamOutputAlphaOptionAll "min(all)"
#define kParamOutputAlphaOptionAllHint "Alpha is set to min(Hue mask,Saturation mask,Brightness mask)"

#define kParamPremultChanged "premultChanged"

enum OutputAlphaEnum {
    eOutputAlphaSource,
    eOutputAlphaHue,
    eOutputAlphaSaturation,
    eOutputAlphaBrightness,
    eOutputAlphaHueSaturation,
    eOutputAlphaHueBrightness,
    eOutputAlphaSaturationBrightness,
    eOutputAlphaAll,
};

using namespace OFX;

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
struct HSVToolValues {
    double hueRange[2];
    double hueRangeWithRolloff[2];
    double hueRotation;
    double hueRolloff;
    double satRange[2];
    double satAdjust;
    double satRolloff;
    double valRange[2];
    double valAdjust;
    double valRolloff;
    HSVToolValues() {
        hueRange[0] = hueRange[1] = 0.;
        hueRangeWithRolloff[0] = hueRangeWithRolloff[1] = 0.;
        hueRotation = 0.;
        hueRolloff = 0.;
        satRange[0] = satRange[1] = 0.;
        satAdjust = 0.;
        satRolloff = 0.;
        valRange[0] = valRange[1] = 0.;
        valAdjust = 0.;
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
bool
angleWithinRange(double h, double h0, double h1)
{
    assert(0 <= h && h <= 360 && 0 <= h0 && h0 <= 360 && 0 <= h1 && h1 <= 360);
    return ((h1 < h0 && (h <= h1 || h0 <= h)) ||
            (h0 <= h && h <= h1));
}

// returns:
// - 0 if outside of [h0, h1]
// - 0 at h0
// - 1 at h1
// - linear from h0 to h1
static inline
double
angleCoeff01(double h, double h0, double h1)
{
    assert(0 <= h && h <= 360 && 0 <= h0 && h0 <= 360 && 0 <= h1 && h1 <= 360);
    if (h1 == (h0 + 360.)) {
        // interval is the whole hue circle
        return 1.;
    }
    if (!angleWithinRange(h, h0, h1)) {
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
    return (h-h0)/(h1-h0);
}

// returns:
// - 0 if outside of [h0, h1]
// - 1 at h0
// - 0 at h1
// - linear from h0 to h1
static inline
double
angleCoeff10(double h, double h0, double h1)
{
    assert(0 <= h && h <= 360 && 0 <= h0 && h0 <= 360 && 0 <= h1 && h1 <= 360);
    if (!angleWithinRange(h, h0, h1)) {
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
    return (h1-h)/(h1-h0);
}

class HSVToolProcessorBase : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImg;
    const OFX::Image *_maskImg;
    OutputAlphaEnum _outputAlpha;
    bool _premult;
    int _premultChannel;
    bool  _doMasking;
    double _mix;
    bool _maskInvert;

public:
    
    HSVToolProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _maskImg(0)
    , _outputAlpha(eOutputAlphaSource)
    , _premult(false)
    , _premultChannel(3)
    , _doMasking(false)
    , _mix(1.)
    , _maskInvert(false)
    , _clampBlack(true)
    , _clampWhite(true)
    {
        assert(angleWithinRange(0, 350, 10));
        assert(angleWithinRange(0, 0, 10));
        assert(!angleWithinRange(0, 5, 10));
        assert(!angleWithinRange(0, 10, 350));
        assert(angleWithinRange(0, 10, 0));
        assert(angleWithinRange(0, 10, 5));
        assert(normalizeAngle(-10) == 350);
        assert(normalizeAngle(-370) == 350);
        assert(normalizeAngle(-730) == 350);
        assert(normalizeAngle(370) == 10);
        assert(normalizeAngle(10) == 10);
        assert(normalizeAngle(730) == 10);
    }

    void setSrcImg(const OFX::Image *v) {_srcImg = v;}
    
    void setMaskImg(const OFX::Image *v, bool maskInvert) { _maskImg = v; _maskInvert = maskInvert; }
    
    void doMasking(bool v) {_doMasking = v;}
    
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
        if (h1 == (h0+360.)) {
            // special case: select any hue (useful to rotate all colors)
            _values.hueRange[0] = 0.;
            _values.hueRange[1] = 360.;
            _values.hueRolloff = 0.;
            _values.hueRangeWithRolloff[0] = 0.;
            _values.hueRangeWithRolloff[1] = 360.;
        } else {
            h0 = normalizeAngle(h0);
            h1 = normalizeAngle(h1);
            if (h1 < h0) {
                std::swap(h0, h1);
            }
            // take the smallest of both angles
            if ((h1 - h0) > 180.) {
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
    }

    void hsvtool(float r, float g, float b, float *hcoeff, float *scoeff, float *vcoeff, float *rout, float *gout, float *bout)
    {
        float h, s, v;
        OFX::Color::rgb_to_hsv(r, g, b, &h, &s, &v);
        const double h0 = _values.hueRange[0];
        const double h1 = _values.hueRange[1];
        const double h0mrolloff = _values.hueRangeWithRolloff[0];
        const double h1prolloff = _values.hueRangeWithRolloff[1];
        // the affected
        if (angleWithinRange(h, h0, h1)) {
            *hcoeff = 1.f;
        } else {
            double c0 = 0.;
            double c1 = 0.;
            // check if we are in the rolloff area
            if (angleWithinRange(h, h0mrolloff, h0)) {
                c0 = angleCoeff01(h, h0mrolloff, h0);
            }
            if (angleWithinRange(h, h1, h1prolloff)) {
                c1 = angleCoeff10(h, h1, h1prolloff);
            }
            *hcoeff = (float)std::max(c0, c1);
        }
        assert(0 <= *hcoeff && *hcoeff <= 1.);
        const double s0 = _values.satRange[0];
        const double s1 = _values.satRange[1];
        const double s0mrolloff = s0 - _values.satRolloff;
        const double s1prolloff = s1 + _values.satRolloff;
        if (s0 <= s && s <= s1) {
            *scoeff = 1.f;
        } else if (s0mrolloff <= s && s <= s0) {
            *scoeff = (float)(s - s0mrolloff) / (float)_values.satRolloff;
        } else if (s1 <= s && s <= s1prolloff) {
            *scoeff = (float)(s1prolloff - s) / (float)_values.satRolloff;
        } else {
            *scoeff = 0.f;
        }
        assert(0 <= *scoeff && *scoeff <= 1.);
        const double v0 = _values.valRange[0];
        const double v1 = _values.valRange[1];
        const double v0mrolloff = v0 - _values.valRolloff;
        const double v1prolloff = v1 + _values.valRolloff;
        if (v0 <= v && v <= v1) {
            *vcoeff = 1.f;
        } else if (v0mrolloff <= v && v <= v0) {
            *vcoeff = (float)(v - v0mrolloff) / (float)_values.valRolloff;
        } else if (v1 <= v && v <= v1prolloff) {
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
            h += coeff * (float)_values.hueRotation;
            s += coeff * (float)_values.satAdjust;
            if (s < 0) {
                s = 0;
            }
            v += coeff * (float)_values.valAdjust;
            OFX::Color::hsv_to_rgb(h, s, v, rout, gout, bout);
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
    }

private:
    HSVToolValues _values;
    bool _clampBlack;
    bool _clampWhite;
};



template <class PIX, int nComponents, int maxValue>
class HSVToolProcessor : public HSVToolProcessorBase
{
public:
    HSVToolProcessor(OFX::ImageEffect &instance)
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
            if (_effect.abort()) {
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
                if (nComponents == 4 && _outputAlpha != eOutputAlphaSource) {
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
                            maskScale = *maskPix/float(maxValue);
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
    }
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class HSVToolPlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    HSVToolPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    , _maskClip(0)
    , _srcColor(0)
    , _dstColor(0)
    , _hueRange(0)
    , _hueRotation(0)
    , _hueRangeRolloff(0)
    , _saturationRange(0)
    , _saturationAdjustment(0)
    , _saturationRangeRolloff(0)
    , _brightnessRange(0)
    , _brightnessAdjustment(0)
    , _brightnessRangeRolloff(0)
    , _clampBlack(0)
    , _clampWhite(0)
    , _outputAlpha(0)
    , _premult(0)
    , _premultChannel(0)
    , _mix(0)
    , _maskApply(0)
    , _maskInvert(0)
    , _premultChanged(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == ePixelComponentRGB ||
                            _dstClip->getPixelComponents() == ePixelComponentRGBA));
        _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert((!_srcClip && getContext() == OFX::eContextGenerator) ||
               (_srcClip && (_srcClip->getPixelComponents() == ePixelComponentRGB ||
                             _srcClip->getPixelComponents() == ePixelComponentRGBA)));
        _maskClip = fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || _maskClip->getPixelComponents() == ePixelComponentAlpha);

        _srcColor = fetchRGBParam(kParamSrcColor);
        _dstColor = fetchRGBParam(kParamDstColor);
        _hueRange = fetchDouble2DParam(kParamHueRange);
        _hueRotation = fetchDoubleParam(kParamHueRotation);
        _hueRangeRolloff = fetchDoubleParam(kParamHueRangeRolloff);
        _saturationRange = fetchDouble2DParam(kParamSaturationRange);
        _saturationAdjustment = fetchDoubleParam(kParamSaturationAdjustment);
        _saturationRangeRolloff = fetchDoubleParam(kParamSaturationRangeRolloff);
        _brightnessRange = fetchDouble2DParam(kParamBrightnessRange);
        _brightnessAdjustment = fetchDoubleParam(kParamBrightnessAdjustment);
        _brightnessRangeRolloff = fetchDoubleParam(kParamBrightnessRangeRolloff);
        assert(_srcColor && _dstColor &&
               _hueRange && _hueRotation && _hueRangeRolloff &&
               _saturationRange && _saturationAdjustment && _saturationRangeRolloff &&
               _brightnessRange && _brightnessAdjustment && _brightnessRangeRolloff);
        _clampBlack = fetchBooleanParam(kParamClampBlack);
        _clampWhite = fetchBooleanParam(kParamClampWhite);
        assert(_clampBlack && _clampWhite);
        _outputAlpha = fetchChoiceParam(kParamOutputAlpha);
        assert(_outputAlpha);
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
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;
    
    /* set up and run a processor */
    void setupAndProcess(HSVToolProcessorBase &, const OFX::RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;
    
    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;
    
private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;
    OFX::Clip *_maskClip;
    OFX::RGBParam *_srcColor;
    OFX::RGBParam *_dstColor;
    OFX::Double2DParam *_hueRange;
    OFX::DoubleParam *_hueRotation;
    OFX::DoubleParam *_hueRangeRolloff;
    OFX::Double2DParam *_saturationRange;
    OFX::DoubleParam *_saturationAdjustment;
    OFX::DoubleParam *_saturationRangeRolloff;
    OFX::Double2DParam *_brightnessRange;
    OFX::DoubleParam *_brightnessAdjustment;
    OFX::DoubleParam *_brightnessRangeRolloff;
    OFX::BooleanParam *_clampBlack;
    OFX::BooleanParam *_clampWhite;
    OFX::ChoiceParam *_outputAlpha;
    OFX::BooleanParam *_premult;
    OFX::ChoiceParam *_premultChannel;
    OFX::DoubleParam *_mix;
    OFX::BooleanParam *_maskApply;
    OFX::BooleanParam *_maskInvert;
    OFX::BooleanParam* _premultChanged; // set to true the first time the user connects src
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
HSVToolPlugin::setupAndProcess(HSVToolProcessorBase &processor, const OFX::RenderArguments &args)
{
    const double time = args.time;
    std::auto_ptr<OFX::Image> dst(_dstClip->fetchImage(time));
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
        (dst->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && dst->getField() != args.fieldToRender)) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    OutputAlphaEnum outputAlpha = (OutputAlphaEnum)_outputAlpha->getValueAtTime(time);
    if (outputAlpha != eOutputAlphaSource) {
        if (dstComponents != OFX::ePixelComponentRGBA) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host dit not take into account output components");
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
            return;
        }
    }

    std::auto_ptr<const OFX::Image> src((_srcClip && _srcClip->isConnected()) ?
                                        _srcClip->fetchImage(time) : 0);
    if (src.get()) {
        if (src->getRenderScale().x != args.renderScale.x ||
            src->getRenderScale().y != args.renderScale.y ||
            (src->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && src->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        // set the components of _dstClip
        if (srcBitDepth != dstBitDepth || (outputAlpha == eOutputAlphaSource && srcComponents != dstComponents)) {            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    bool doMasking = ((!_maskApply || _maskApply->getValueAtTime(time)) && _maskClip && _maskClip->isConnected());
    std::auto_ptr<const OFX::Image> mask(doMasking ? _maskClip->fetchImage(time) : 0);
    if (doMasking) {
        if (mask.get()) {
            if (mask->getRenderScale().x != args.renderScale.x ||
                mask->getRenderScale().y != args.renderScale.y ||
                (mask->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && mask->getField() != args.fieldToRender)) {
                setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                OFX::throwSuiteStatusException(kOfxStatFailed);
            }
        }
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }
    
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());
    processor.setRenderWindow(args.renderWindow);

    HSVToolValues values;
    _hueRange->getValueAtTime(time, values.hueRange[0], values.hueRange[1]);
    values.hueRangeWithRolloff[0] = values.hueRangeWithRolloff[1] = 0; // set in setValues()
    _hueRotation->getValueAtTime(time, values.hueRotation);
    _hueRangeRolloff->getValueAtTime(time, values.hueRolloff);
    _saturationRange->getValueAtTime(time, values.satRange[0], values.satRange[1]);
    _saturationAdjustment->getValueAtTime(time, values.satAdjust);
    _saturationRangeRolloff->getValueAtTime(time, values.satRolloff);
    _brightnessRange->getValueAtTime(time, values.valRange[0], values.valRange[1]);
    _brightnessAdjustment->getValueAtTime(time, values.valAdjust);
    _brightnessRangeRolloff->getValueAtTime(time, values.valRolloff);

    bool clampBlack,clampWhite;
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
}

// the overridden render function
void
HSVToolPlugin::render(const OFX::RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();
    
    assert(kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth());
    assert(dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA);
    if (dstComponents == OFX::ePixelComponentRGBA) {
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte: {
                HSVToolProcessor<unsigned char, 4, 255> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort: {
                HSVToolProcessor<unsigned short, 4, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat: {
                HSVToolProcessor<float, 4, 1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else {
        assert(dstComponents == OFX::ePixelComponentRGB);
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte: {
                HSVToolProcessor<unsigned char, 3, 255> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort: {
                HSVToolProcessor<unsigned short, 3, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat: {
                HSVToolProcessor<float, 3, 1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}

bool
HSVToolPlugin::isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &/*identityTime*/)
{
    if (!_srcClip) {
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
            switch(outputAlpha) {
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
    if (hueRotation == 0. && saturationAdjustment == 0. && brightnessAdjustment == 0.) {
        identityClip = _srcClip;
        return true;
    }

    bool doMasking = ((!_maskApply || _maskApply->getValueAtTime(time)) && _maskClip && _maskClip->isConnected());
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        if (!maskInvert) {
            OfxRectI maskRoD;
            OFX::Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
            // effect is identity if the renderWindow doesn't intersect the mask RoD
            if (!OFX::Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0)) {
                identityClip = _srcClip;
                return true;
            }
        }
    }

    return false;
}

void
HSVToolPlugin::changedParam(const InstanceChangedArgs &args, const std::string &paramName)
{
    const double time = args.time;
    if (paramName == kParamSrcColor && args.reason == OFX::eChangeUserEdit) {
        // - when setting srcColor: compute hueRange, satRange, valRange (as empty ranges), set rolloffs to (50,0.3,0.3)
        double r, g, b;
        _srcColor->getValueAtTime(time, r, g, b);
        float h, s, v;
        OFX::Color::rgb_to_hsv((float)r, (float)g, (float)b, &h, &s, &v);
        _hueRange->setValue(h, h);
        _hueRangeRolloff->setValue(50.);
        _saturationRange->setValue(s, s);
        _saturationRangeRolloff->setValue(0.3);
        _brightnessRange->setValue(v, v);
        _brightnessRangeRolloff->setValue(0.3);
    }
    if (paramName == kParamDstColor && args.reason == OFX::eChangeUserEdit) {
        // - when setting dstColor: compute hueRotation, satAdjust and valAdjust
        double r, g, b;
        _srcColor->getValueAtTime(time, r, g, b);
        float h, s, v;
        OFX::Color::rgb_to_hsv((float)r, (float)g, (float)b, &h, &s, &v);
        double tor, tog, tob;
        _dstColor->getValueAtTime(time, tor, tog, tob);
        float toh, tos, tov;
        OFX::Color::rgb_to_hsv((float)tor, (float)tog, (float)tob, &toh, &tos, &tov);
        double dh = toh - h;
        while (dh <= -180.) {
            dh += 360;
        }
        while (dh > 180.) {
            dh -= 360;
        }
        _hueRotation->setValue(dh);
        _saturationAdjustment->setValue(tos - s);
        _brightnessAdjustment->setValue(tov - v);
    } else if (paramName == kParamPremult && args.reason == OFX::eChangeUserEdit) {
        _premultChanged->setValue(true);
    }
}

void
HSVToolPlugin::changedClip(const InstanceChangedArgs &args, const std::string &clipName)
{
    if (clipName == kOfxImageEffectSimpleSourceClipName &&
        _srcClip && _srcClip->isConnected() &&
        !_premultChanged->getValue() &&
        args.reason == OFX::eChangeUserEdit) {
        switch (_srcClip->getPreMultiplication()) {
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



/* Override the clip preferences */
void
HSVToolPlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
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

mDeclarePluginFactory(HSVToolPluginFactory, {}, {});

void
HSVToolPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
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
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentRGBA);
#endif
}

void
HSVToolPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
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
        }

        {
            RGBParamDescriptor *param = desc.defineRGBParam(kParamSrcColor);
            param->setLabel(kParamSrcColorLabel);
            param->setHint(kParamSrcColorHint);
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
            param->setLayoutHint(eLayoutHintDivider); // last parameter in the group
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        if (page && group) {
            page->addChild(*group);
        }
    }

    {
        GroupParamDescriptor *group = desc.defineGroupParam(kGroupHue);
        if (group) {
            group->setLabel(kGroupHueLabel);
            group->setHint(kGroupHueHint);
            group->setEnabled(true);
        }

        {
            Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamHueRange);
            param->setLabel(kParamHueRangeLabel);
            param->setHint(kParamHueRangeHint);
            param->setDimensionLabels("", ""); // the two values have the same meaning (they just define a range)
            param->setDefault(0., 360.);
            param->setDisplayRange(0., 0., 360., 360.);
            param->setDoubleType(eDoubleTypeAngle);
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

        if (page && group) {
            page->addChild(*group);
        }
    }

    {
        GroupParamDescriptor *group = desc.defineGroupParam(kGroupSaturation);
        if (group) {
            group->setLabel(kGroupSaturationLabel);
            group->setHint(kGroupSaturationHint);
            group->setEnabled(true);
        }

        {
            Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamSaturationRange);
            param->setLabel(kParamSaturationRangeLabel);
            param->setHint(kParamSaturationRangeHint);
            param->setDimensionLabels("", ""); // the two values have the same meaning (they just define a range)
            param->setDefault(0., 1.);
            param->setDisplayRange(0., 0., 1, 1);
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
            param->setDisplayRange(0., 1.);
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
            param->setDisplayRange(0., 1.);
            param->setLayoutHint(eLayoutHintDivider); // last parameter in the group
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        if (page && group) {
            page->addChild(*group);
        }
    }

    {
        GroupParamDescriptor *group = desc.defineGroupParam(kGroupBrightness);
        if (group) {
            group->setLabel(kGroupBrightnessLabel);
            group->setHint(kGroupBrightnessHint);
            group->setEnabled(true);
        }

        {
            Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamBrightnessRange);
            param->setLabel(kParamBrightnessRangeLabel);
            param->setHint(kParamBrightnessRangeHint);
            param->setDimensionLabels("", ""); // the two values have the same meaning (they just define a range)
            param->setDefault(0., 1.);
            param->setDisplayRange(0., 0., 1, 1);
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
            param->setDisplayRange(0., 1.);
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
            param->setDisplayRange(0., 1.);
            param->setLayoutHint(eLayoutHintDivider); // last parameter in the group
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        if (page && group) {
            page->addChild(*group);
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
        param->appendOption(kParamOutputAlphaOptionSource, kParamOutputAlphaOptionSourceHint);
        assert(param->getNOptions() == (int)eOutputAlphaHue);
        param->appendOption(kParamOutputAlphaOptionHue, kParamOutputAlphaOptionHueHint);
        assert(param->getNOptions() == (int)eOutputAlphaSaturation);
        param->appendOption(kParamOutputAlphaOptionSaturation, kParamOutputAlphaOptionSaturationHint);
        assert(param->getNOptions() == (int)eOutputAlphaBrightness);
        param->appendOption(kParamOutputAlphaOptionBrightness, kParamOutputAlphaOptionBrightnessHint);
        assert(param->getNOptions() == (int)eOutputAlphaHueSaturation);
        param->appendOption(kParamOutputAlphaOptionHueSaturation, kParamOutputAlphaOptionHueSaturationHint);
        assert(param->getNOptions() == (int)eOutputAlphaHueBrightness);
        param->appendOption(kParamOutputAlphaOptionHueBrightness, kParamOutputAlphaOptionHueBrightnessHint);
        assert(param->getNOptions() == (int)eOutputAlphaSaturationBrightness);
        param->appendOption(kParamOutputAlphaOptionSaturationBrightness, kParamOutputAlphaOptionSaturationBrightnessHint);
        assert(param->getNOptions() == (int)eOutputAlphaAll);
        param->appendOption(kParamOutputAlphaOptionAll, kParamOutputAlphaOptionAllHint);
        param->setDefault((int)eOutputAlphaHue);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }

    ofxsPremultDescribeParams(desc, page);
    ofxsMaskMixDescribeParams(desc, page);

    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamPremultChanged);
        param->setDefault(false);
        param->setIsSecret(true);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }
}

OFX::ImageEffect*
HSVToolPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new HSVToolPlugin(handle);
}


static HSVToolPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)
