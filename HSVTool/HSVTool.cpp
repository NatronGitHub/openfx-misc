/*
 OFX HSVTool plugin.
 
 Copyright (C) 2014 INRIA
 
 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:
 
 Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.
 
 Redistributions in binary form must reproduce the above copyright notice, this
 list of conditions and the following disclaimer in the documentation and/or
 other materials provided with the distribution.
 
 Neither the name of the {organization} nor the names of its
 contributors may be used to endorse or promote products derived from
 this software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 
 INRIA
 Domaine de Voluceau
 Rocquencourt - B.P. 105
 78153 Le Chesnay Cedex - France
 
 
 The skeleton for this source file is from:
 OFX Basic Example plugin, a plugin that illustrates the use of the OFX Support library.
 
 Copyright (C) 2004-2005 The Open Effects Association Ltd
 Author Bruno Nicoletti bruno@thefoundry.co.uk
 
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
 
 * Redistributions of source code must retain the above copyright notice,
 this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.
 * Neither the name The Open Effects Association Ltd, nor the names of its
 contributors may be used to endorse or promote products derived from this
 software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 
 The Open Effects Association Ltd
 1 Wardour St
 London W1D 6PA
 England
 
 */

#include "HSVTool.h"

#include <cmath>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"
#include "ofxsLut.h"

#define kPluginName "HSVToolOFX"
#define kPluginGrouping "Color"
#define kPluginDescription "Adjust hue, saturation and brightnes, or perform color replacement."
#define kPluginIdentifier "net.sf.openfx.HSVToolPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
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
#define kParamHueRangeHint "Range of color hues that are modified (in degrees). Red is 0, green is 120, blue is 240. The affected hue range is the smallest interval. For example, if the range is (12, 348), then the selected range is red plus or minus 12 degrees."
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
#define kParamSaturationAdjustmentHint "Adjustment of color saturations within the range."
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
};

//
static inline
double
normalizeAngle(double a)
{
    int c = std::floor(a / 360);
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
    bool   _doMasking;
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
        double h0 = normalizeAngle(_values.hueRange[0]);
        double h1 = normalizeAngle(_values.hueRange[1]);
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
            *hcoeff = 1.;
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
            *hcoeff = std::max(c0, c1);
        }
        assert(0 <= hcoeff <= 1.);
        const double s0 = _values.satRange[0];
        const double s1 = _values.satRange[1];
        const double s0mrolloff = s0 - _values.satRolloff;
        const double s1prolloff = s1 + _values.satRolloff;
        if (s0 <= s && s <= s1) {
            *scoeff = 1.;
        } else if (s0mrolloff <= s && s <= s0) {
            *scoeff = (s - s0mrolloff) / _values.satRolloff;
        } else if (s1 <= s && s <= s1prolloff) {
            *scoeff = (s1prolloff - s) / _values.satRolloff;
        } else {
            *scoeff = 0.;
        }
        assert(0 <= scoeff <= 1.);
        const double v0 = _values.valRange[0];
        const double v1 = _values.valRange[1];
        const double v0mrolloff = v0 - _values.valRolloff;
        const double v1prolloff = v1 + _values.valRolloff;
        if (v0 <= v && v <= v1) {
            *vcoeff = 1.;
        } else if (v0mrolloff <= v && v <= v0) {
            *vcoeff = (v - v0mrolloff) / _values.valRolloff;
        } else if (v1 <= v && v <= v1prolloff) {
            *vcoeff = (v1prolloff - v) / _values.valRolloff;
        } else {
            *vcoeff = 0.;
        }
        assert(0 <= vcoeff <= 1.);
        double coeff = std::min(std::min(*hcoeff, *scoeff), *vcoeff);
        assert(0 <= coeff <= 1.);
        if (coeff <= 0.) {
            *rout = r;
            *gout = g;
            *bout = b;
        } else {
            h += coeff * _values.hueRotation;
            s += coeff * _values.satAdjust;
            v += coeff * _values.valAdjust;
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
                ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, premultOut, _premultChannel, x, y, srcPix, _doMasking, _maskImg, _mix, _maskInvert, dstPix);
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
                            maskScale = _maskInvert ? 1. : 0.;
                        } else {
                            maskScale = *maskPix/float(maxValue);
                            if (_maskInvert) {
                                maskScale = 1. - maskScale;
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
    , dstClip_(0)
    , srcClip_(0)
    , maskClip_(0)
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
    , _maskInvert(0)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        assert(dstClip_ && (dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA));
        srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(srcClip_ && (srcClip_->getPixelComponents() == ePixelComponentRGB || srcClip_->getPixelComponents() == ePixelComponentRGBA));
        maskClip_ = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!maskClip_ || maskClip_->getPixelComponents() == ePixelComponentAlpha);

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
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
    }
    
private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;
    
    /* set up and run a processor */
    void setupAndProcess(HSVToolProcessorBase &, const OFX::RenderArguments &args);

    //virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    OFX::Clip *maskClip_;
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
    OFX::BooleanParam *_maskInvert;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
HSVToolPlugin::setupAndProcess(HSVToolProcessorBase &processor, const OFX::RenderArguments &args)
{
    std::auto_ptr<OFX::Image> dst(dstClip_->fetchImage(args.time));
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dst->getRenderScale().x != args.renderScale.x ||
        dst->getRenderScale().y != args.renderScale.y ||
        dst->getField() != args.fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
    std::auto_ptr<const OFX::Image> src(srcClip_->fetchImage(args.time));
    if (src.get()) {
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    std::auto_ptr<OFX::Image> mask(getContext() != OFX::eContextFilter ? maskClip_->fetchImage(args.time) : 0);
    if (getContext() != OFX::eContextFilter && maskClip_->isConnected()) {
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }
    
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());
    processor.setRenderWindow(args.renderWindow);

    const double time = args.time;
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
    _clampBlack->getValueAtTime(args.time, clampBlack);
    _clampWhite->getValueAtTime(args.time, clampWhite);
    int outputAlpha_i;
    _outputAlpha->getValueAtTime(args.time, outputAlpha_i);
    OutputAlphaEnum outputAlpha = (OutputAlphaEnum)outputAlpha_i;
    bool premult;
    int premultChannel;
    _premult->getValueAtTime(args.time, premult);
    _premultChannel->getValueAtTime(args.time, premultChannel);
    double mix;
    _mix->getValueAtTime(args.time, mix);
    
    processor.setValues(values, clampBlack, clampWhite, outputAlpha, premult, premultChannel, mix);
    processor.process();
}

// the overridden render function
void
HSVToolPlugin::render(const OFX::RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();
    
    assert(dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA);
    if (dstComponents == OFX::ePixelComponentRGBA) {
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte :
            {
                HSVToolProcessor<unsigned char, 4, 255> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort :
            {
                HSVToolProcessor<unsigned short, 4, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat :
            {
                HSVToolProcessor<float,4,1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else {
        assert(dstComponents == OFX::ePixelComponentRGB);
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte :
            {
                HSVToolProcessor<unsigned char, 3, 255> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort :
            {
                HSVToolProcessor<unsigned short, 3, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat :
            {
                HSVToolProcessor<float,3,1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}

#if 0
bool
HSVToolPlugin::isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &/*identityTime*/)
{
    double mix;
    _mix->getValueAtTime(args.time, mix);

    if (mix == 0.) {
        identityClip = srcClip_;
        return true;
    }


    if (srcClip_->getPixelComponents() == ePixelComponentRGBA) {
        // check cases where alpha is affected, even if colors don't change
        int outputAlpha_i;
        _outputAlpha->getValueAtTime(args.time, outputAlpha_i);
        OutputAlphaEnum outputAlpha = (OutputAlphaEnum)outputAlpha_i;
        if (outputAlpha != eOutputAlphaSource) {
            double hueMin, hueMax;
            _hueRange->getValueAtTime(args.time, hueMin, hueMax);
            bool alphaHue = (hueMin != 0. || hueMax != 360.);
            double satMin, satMax;
            _saturationRange->getValueAtTime(args.time, satMin, satMax);
            bool alphaSat = (satMin != 0. || satMax != 1.);
            double valMin, valMax;
            _brightnessRange->getValueAtTime(args.time, valMin, valMax);
            bool alphaVal = (valMin != 0. || valMax != 1.);
            switch(outputAlpha) {
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
    _hueRotation->getValueAtTime(args.time, hueRotation);
    double saturationAdjustment;
    _saturationAdjustment->getValueAtTime(args.time, saturationAdjustment);
    double brightnessAdjustment;
    _brightnessAdjustment->getValueAtTime(args.time, brightnessAdjustment);
    if (hueRotation == 0. && saturationAdjustment == 0. && brightnessAdjustment == 0.) {
        identityClip = srcClip_;
        return true;
    }

    return false;
}
#endif

void
HSVToolPlugin::changedParam(const InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kParamSrcColor && args.reason == OFX::eChangeUserEdit) {
        // - when setting srcColor: compute hueRange, satRange, valRange (as empty ranges), set rolloffs to (50,0.3,0.3)
        double r, g, b;
        _srcColor->getValueAtTime(args.time, r, g, b);
        float h, s, v;
        OFX::Color::rgb_to_hsv(r, g, b, &h, &s, &v);
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
        _srcColor->getValueAtTime(args.time, r, g, b);
        float h, s, v;
        OFX::Color::rgb_to_hsv(r, g, b, &h, &s, &v);
        double tor, tog, tob;
        _dstColor->getValueAtTime(args.time, tor, tog, tob);
        float toh, tos, tov;
        OFX::Color::rgb_to_hsv(tor, tog, tob, &toh, &tos, &tov);
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
    }
}

void
HSVToolPlugin::changedClip(const InstanceChangedArgs &args, const std::string &clipName)
{
    if (clipName == kOfxImageEffectSimpleSourceClipName && srcClip_ && args.reason == OFX::eChangeUserEdit) {
        switch (srcClip_->getPreMultiplication()) {
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
    // set the components of dstClip_
    int outputAlpha_i;
    _outputAlpha->getValue(outputAlpha_i);
    OutputAlphaEnum outputAlpha = (OutputAlphaEnum)outputAlpha_i;
    if (outputAlpha != eOutputAlphaSource) {
        // output must be RGBA, output image is unpremult
        clipPreferences.setClipComponents(*dstClip_, ePixelComponentRGBA);
        clipPreferences.setOutputPremultiplication(eImageUnPreMultiplied);
    }
}

mDeclarePluginFactory(HSVToolPluginFactory, {}, {});

void
HSVToolPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
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
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(kRenderThreadSafety);
    
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
    
    if (context == eContextGeneral || context == eContextPaint) {
        ClipDescriptor *maskClip = context == eContextGeneral ? desc.defineClip("Mask") : desc.defineClip("Brush");
        maskClip->addSupportedComponent(ePixelComponentAlpha);
        maskClip->setTemporalClipAccess(false);
        if (context == eContextGeneral)
            maskClip->setOptional(true);
        maskClip->setSupportsTiles(kSupportsTiles);
        maskClip->setIsMask(true);
    }
    
    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
    
    {
        GroupParamDescriptor *group = desc.defineGroupParam(kGroupColorReplacement);
        group->setLabels(kGroupColorReplacementLabel, kGroupColorReplacementLabel, kGroupColorReplacementLabel);
        group->setHint(kGroupColorReplacementHint);
        group->setEnabled(true);
        {
            RGBParamDescriptor *param = desc.defineRGBParam(kParamSrcColor);
            param->setLabels(kParamSrcColorLabel, kParamSrcColorLabel, kParamSrcColorLabel);
            param->setHint(kParamSrcColorHint);
            page->addChild(*param);
            param->setParent(*group);
        }
        {
            RGBParamDescriptor *param = desc.defineRGBParam(kParamDstColor);
            param->setLabels(kParamDstColorLabel, kParamDstColorLabel, kParamDstColorLabel);
            param->setHint(kParamDstColorHint);
            page->addChild(*param);
            param->setParent(*group);
            param->setLayoutHint(eLayoutHintDivider); // last parameter in the group
        }
        page->addChild(*group);
    }

    {
        GroupParamDescriptor *group = desc.defineGroupParam(kGroupHue);
        group->setLabels(kGroupHueLabel, kGroupHueLabel, kGroupHueLabel);
        group->setHint(kGroupHueHint);
        group->setEnabled(true);
        {
            Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamHueRange);
            param->setLabels(kParamHueRangeLabel, kParamHueRangeLabel, kParamHueRangeLabel);
            param->setHint(kParamHueRangeHint);
            param->setDimensionLabels("", ""); // the two values have the same meaning (they just define a range)
            param->setDefault(0., 360.);
            param->setDisplayRange(0., 0., 360., 360.);
            param->setDoubleType(eDoubleTypeAngle);
            page->addChild(*param);
            param->setParent(*group);
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamHueRotation);
            param->setLabels(kParamHueRotationLabel, kParamHueRotationLabel, kParamHueRotationLabel);
            param->setHint(kParamHueRotationHint);
            param->setDisplayRange(-180., 180.);
            param->setDoubleType(eDoubleTypeAngle);
            page->addChild(*param);
            param->setParent(*group);
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamHueRangeRolloff);
            param->setLabels(kParamHueRangeRolloffLabel, kParamHueRangeRolloffLabel, kParamHueRangeRolloffLabel);
            param->setHint(kParamHueRangeRolloffHint);
            param->setRange(0., 180.);
            param->setDisplayRange(0., 180.);
            param->setDoubleType(eDoubleTypeAngle);
            page->addChild(*param);
            param->setParent(*group);
            param->setLayoutHint(eLayoutHintDivider); // last parameter in the group
        }

        page->addChild(*group);
    }

    {
        GroupParamDescriptor *group = desc.defineGroupParam(kGroupSaturation);
        group->setLabels(kGroupSaturationLabel, kGroupSaturationLabel, kGroupSaturationLabel);
        group->setHint(kGroupSaturationHint);
        group->setEnabled(true);
        {
            Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamSaturationRange);
            param->setLabels(kParamSaturationRangeLabel, kParamSaturationRangeLabel, kParamSaturationRangeLabel);
            param->setHint(kParamSaturationRangeHint);
            param->setDimensionLabels("", ""); // the two values have the same meaning (they just define a range)
            param->setDefault(0., 1.);
            param->setDisplayRange(0., 0., 1, 1);
            page->addChild(*param);
            param->setParent(*group);
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSaturationAdjustment);
            param->setLabels(kParamSaturationAdjustmentLabel, kParamSaturationAdjustmentLabel, kParamSaturationAdjustmentLabel);
            param->setHint(kParamSaturationAdjustmentHint);
            param->setDisplayRange(0., 1.);
            page->addChild(*param);
            param->setParent(*group);
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSaturationRangeRolloff);
            param->setLabels(kParamSaturationRangeRolloffLabel, kParamSaturationRangeRolloffLabel, kParamSaturationRangeRolloffLabel);
            param->setHint(kParamSaturationRangeRolloffHint);
            param->setDisplayRange(0., 1.);
            page->addChild(*param);
            param->setParent(*group);
            param->setLayoutHint(eLayoutHintDivider); // last parameter in the group
        }

        page->addChild(*group);
    }

    {
        GroupParamDescriptor *group = desc.defineGroupParam(kGroupBrightness);
        group->setLabels(kGroupBrightnessLabel, kGroupBrightnessLabel, kGroupBrightnessLabel);
        group->setHint(kGroupBrightnessHint);
        group->setEnabled(true);
        {
            Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamBrightnessRange);
            param->setLabels(kParamBrightnessRangeLabel, kParamBrightnessRangeLabel, kParamBrightnessRangeLabel);
            param->setHint(kParamBrightnessRangeHint);
            param->setDimensionLabels("", ""); // the two values have the same meaning (they just define a range)
            param->setDefault(0., 1.);
            param->setDisplayRange(0., 0., 1, 1);
            page->addChild(*param);
            param->setParent(*group);
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamBrightnessAdjustment);
            param->setLabels(kParamBrightnessAdjustmentLabel, kParamBrightnessAdjustmentLabel, kParamBrightnessAdjustmentLabel);
            param->setHint(kParamBrightnessAdjustmentHint);
            param->setDisplayRange(0., 1.);
            page->addChild(*param);
            param->setParent(*group);
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamBrightnessRangeRolloff);
            param->setLabels(kParamBrightnessRangeRolloffLabel, kParamBrightnessRangeRolloffLabel, kParamBrightnessRangeRolloffLabel);
            param->setHint(kParamBrightnessRangeRolloffHint);
            param->setDisplayRange(0., 1.);
            page->addChild(*param);
            param->setParent(*group);
            param->setLayoutHint(eLayoutHintDivider); // last parameter in the group
        }

        page->addChild(*group);
    }

    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamClampBlack);
        param->setLabels(kParamClampBlackLabel, kParamClampBlackLabel, kParamClampBlackLabel);
        param->setHint(kParamClampBlackHint);
        param->setDefault(true);
        param->setAnimates(true);
        page->addChild(*param);
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamClampWhite);
        param->setLabels(kParamClampWhiteLabel, kParamClampWhiteLabel, kParamClampWhiteLabel);
        param->setHint(kParamClampWhiteHint);
        param->setDefault(false);
        param->setAnimates(true);
        page->addChild(*param);
    }

    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamOutputAlpha);
        param->setLabels(kParamOutputAlphaLabel, kParamOutputAlphaLabel, kParamOutputAlphaLabel);
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
        page->addChild(*param);
    }

    ofxsPremultDescribeParams(desc, page);
    ofxsMaskMixDescribeParams(desc, page);
}

OFX::ImageEffect*
HSVToolPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new HSVToolPlugin(handle);
}

void getHSVToolPluginID(OFX::PluginFactoryArray &ids)
{
    static HSVToolPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

