/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/NatronGitHub/openfx-misc>,
 * (C) 2018-2021 The Natron Developers
 * (C) 2013-2018 INRIA
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
 * OFX Keyer plugin.
 */

#include <cmath>
#include <cfloat> // FLT_MAX
#include <limits>
#include <algorithm>

#include "ofxsProcessing.H"
#include "ofxsLut.h"
#include "ofxsMacros.h"
#include "ofxsNonFinite.h" // clampToFloatRange, clampToUnit, finiteSample

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "KeyerOFX"
#define kPluginGrouping "Keyer"
#define kPluginDescriptionHead \
    "A collection of simple keyers. These work by computing a foreground key from the RGB values of the input image (see the keyerMode parameter).\n" \
    "This foreground key is a scalar from 0 to 1. From the foreground key, a key (from 0 to 1) is computed, which gives the output alpha (see below).\n" \
    "The function that maps the foreground key to the key is piecewise linear:\n" \
    "- it is 0 below A = (center+toleranceLower+softnessLower)\n" \
    "- it is linear between A = (center+toleranceLower+softnessLower) and B = (center+toleranceLower)\n" \
    "- it is 1 between B = (center+toleranceLower) and C = (center+toleranceUpper)\n" \
    "- it is linear between C = (center+toleranceUpper) and D = (center+toleranceUpper+softnessUpper)\n" \
    "- it is 0 above D = (center+toleranceUpper+softnessUpper)\n" \
    "Two special cases: if B <= 0, the key is 1 for every foreground key <= 0, even below A; if C >= 1, it is 1 for every foreground key above C, even above D.\n"
#define kPluginDescriptionPolarityV1 \
    "The key is the background key (transparency): the output alpha is 1 minus the key, 0 (transparent) between B and C, and 1 (opaque) below A and above D. " \
    "Setting the key color sets A, B, C, D around it, so that it is transparent. Invert inverts the matte.\n"
#define kPluginDescriptionPolarity \
    "In the Luminance, Color, Saturation, Min and Max modes, the output alpha is the key: it is 1 (opaque) between B and C, and 0 (transparent) below A and above D, as in Foundry's Keyer node, where \"any pixel with a luminance value between B and C becomes opaque\". " \
    "In the Screen mode, the output alpha is 1 minus the key: the pixels of the key color are transparent. Invert inverts the matte.\n" \
    "With the default A, B, C, D (0, 1, 1, 1), the alpha is the foreground key, clamped to [0, 1].\n" \
    "Setting the key color sets A, B, C, D so that it is transparent: in the Screen mode, the key is 1 around it; in the other modes, the alpha goes from 0 at the key color to 1 at 1 (A is the foreground key of the key color, in [0, 1], and B = C = D = 1).\n"
#define kPluginDescriptionTail \
    "\n" \
    "Keyer can pull mattes that correspond to the RGB channels, the luminance and the red, green and blue colors. " \
    "One very useful application for a luminance mask is to mask out a sky (almost always it is the brightest thing in a landscape).\n" \
    "Conversion from A, B, C, D to Keyer parameters is:\n" \
    "softnessLower = (A-B)\n" \
    "toleranceLower = (B-C)/2\n" \
    "center = (B+C)/2\n" \
    "toleranceUpper = (C-B)/2\n" \
    "softnessUpper = (D-C)\n"
#define kPluginDescriptionNoteV1 \
    "Note that the matte is inverted with respect to the A, B, C, D of Foundry's documentation of the Keyer node, where \"any pixel with a luminance value between B and C becomes opaque\" (version 2 follows it).\n"
#define kPluginDescriptionSeeAlso \
    "\n" \
    "See also:\n" \
    "- https://learn.foundry.com/nuke/content/reference_guide/keyer_nodes/keyer.html\n" \
    "- https://web.archive.org/web/20220524000628/http://www.opticalenquiry.com/nuke/index.php?title=The_Keyer_Nodes#Keyer" \
    "\n" \
    "- https://web.archive.org/web/20211023071843/http://opticalenquiry.com/nuke/index.php?title=Keying_Tips"
#define kPluginDescriptionV1 kPluginDescriptionHead kPluginDescriptionPolarityV1 kPluginDescriptionTail kPluginDescriptionNoteV1 kPluginDescriptionSeeAlso
#define kPluginDescription kPluginDescriptionHead kPluginDescriptionPolarity kPluginDescriptionTail kPluginDescriptionSeeAlso

#define kPluginIdentifier "net.sf.openfx.KeyerPlugin"
// history:
// 1.0 initial version
// 1.1 non-finite input samples give a finite output and a matte in [0,1] (issue #195): a NaN
//     sample is keyed as 0, +-inf as +-FLT_MAX (an infinite pixel gave inf * 0 = NaN in the despill,
//     e.g. Screen mode with keyColor (0,0,1) and pixel (0,0,inf)); the Composite output is clamped
//     to the float range. The output for finite inputs is unchanged.
//     In the filter context, Source is RGBA and the Output components are those of Source, as the
//     OpenFX spec mandates (issue #190; the Output was forced to RGBA). With an RGB Source (general
//     context), "Add to Inside Mask" and "Normal" source alpha treat it as opaque (they read the
//     next pixel's red as alpha).
//     With a key colour component beyond about 1.8e19, the squared norm of the key colour, computed
//     in float, overflowed to inf (issue #213): in Screen mode, the matte was wrong (the distance to
//     the key colour axis was |fg|), and the despill of the Screen and None modes was skipped. It is
//     now computed in double when the float one overflows. A key colour beyond the float range is
//     clamped to it. The output is unchanged for the key colours whose float squared norm is finite.
//     New "Invert" parameter (issue #280), off by default: it inverts the key computed from the
//     foreground key, before the inside and outside masks are applied, so that the pixels whose
//     foreground key is between B and C become opaque, as with Foundry's Keyer node. The despill
//     amount uses the key without Invert. Existing projects render the same.
// 2.0 the matte follows Foundry's documentation of the Keyer node (issue #280): in the Luminance and Color modes, the
//     output alpha is the background key (opaque between B and C), and the default A, B, C, D are
//     0, 1, 1, 1 (softnessLower -1, softnessUpper 0), so that the default alpha is the luminance,
//     clamped to [0, 1]. The Screen mode keeps the polarity of version 1 (the key color is
//     transparent). New Saturation, Min and Max keyer modes. Version 1 is kept (deprecated), with its
//     polarity and defaults, so that old projects render the same.
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.
#define kPluginVersionMinorV1 1 // the minor version of the deprecated version 1

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

/*
   Simple Luma/Color/Screen Keyer.
 */

#define kParamKeyColor "keyColor"
#define kParamKeyColorLabel "Key Color"
#define kParamKeyColorHint \
    "Foreground key color: foreground areas containing the key color are replaced with the background image. Setting it sets the softness, tolerance and center parameters so that the key color is transparent (see the description)."

#define kParamKeyerMode "mode"
#define kParamKeyerModeLabel "Keyer Mode"
#define kParamKeyerModeHint \
    "The operation used to compute the foreground key."
#define kParamKeyerModeOptionLuminance "Luminance", "Use the luminance for keying. The foreground key value is in luminance.", "luminance"
#define kParamKeyerModeOptionColor "Color", "Use the color for keying. If the key color is pure green, this corresponds a green keyer, etc.", "color"
#define kParamKeyerModeOptionScreen "Screen", "Use the color minus the other components for keying. If the key color is pure green, this corresponds a greenscreen, etc. The output alpha is 1 minus the key, so that the key color is transparent, and the upper tolerance and softness are ignored (the key is 1 above C).", "screen"
#define kParamKeyerModeOptionNone "None", "No keying, just despill color values. You can control despill areas using either set the inside mask, or use with 'Source Alpha' set to 'Add to Inside Mask'. If 'Output Mode' is set to 'Unpremultiplied', this despills the image even if no mask is present.", "none"
#define kParamKeyerModeOptionSaturation "Saturation", "Use the saturation for keying: (max - min) / max of R, G and B (0 if max is zero or negative).", "saturation"
#define kParamKeyerModeOptionMin "Min", "Use the minimum of R, G and B for keying.", "min"
#define kParamKeyerModeOptionMax "Max", "Use the maximum of R, G and B for keying.", "max"
#define kParamKeyerModeDefault eKeyerModeLuminance
#define kParamKeyerModeDefaultString "Luminance"
enum KeyerModeEnum
{
    eKeyerModeLuminance,
    eKeyerModeColor,
    eKeyerModeScreen,
    eKeyerModeNone,
    // version 2 only
    eKeyerModeSaturation,
    eKeyerModeMin,
    eKeyerModeMax,
};

// the foreground key of the Saturation, Min and Max modes
double
rgbKeyValue(KeyerModeEnum keyerMode,
            double r,
            double g,
            double b)
{
    const double mx = (std::max)( r, (std::max)(g, b) );
    const double mn = (std::min)( r, (std::min)(g, b) );
    switch (keyerMode) {
    case eKeyerModeSaturation:
        // (with a very negative mn and a tiny mx, the quotient may exceed the float range: kept in it)
        return (mx > 0.) ? clampToFloatRange( (mx - mn) / mx ) : 0.;
    case eKeyerModeMin:
        return mn;
    case eKeyerModeMax:
        return mx;
    default:
        assert(false);

        return 0.;
    }
}

// kParamLuminanceMath*, LuminanceMathEnum and luminance() are in ofxsLut.h
#define kParamLuminanceMathHint "Formula used to compute luminance from RGB values."


#define kParamSoftnessLower "softnessLower"
#define kParamSoftnessLowerLabel "Softness (lower)"
#define kParamSoftnessLowerHint "Width of the lower softness range [A, B] = [center+toleranceLower+softnessLower, center+toleranceLower] (softnessLower is negative). The key goes from 0 to 1 when the foreground key is over this range."

#define kParamToleranceLower "toleranceLower"
#define kParamToleranceLowerLabel "Tolerance (lower)"
#define kParamToleranceLowerHint "Width of the lower tolerance range [B, center] = [center+toleranceLower, center] (toleranceLower is negative). The key is 1 when the foreground key is over this range."

#define kParamCenter "center"
#define kParamCenterLabel "Center"
#define kParamCenterHint "Foreground key value at the center of the range [B, C] where the key is 1."

#define kParamToleranceUpper "toleranceUpper"
#define kParamToleranceUpperLabel "Tolerance (upper)"
#define kParamToleranceUpperHint "Width of the upper tolerance range [center, C] = [center, center+toleranceUpper]. The key is 1 when the foreground key is over this range. Ignored in Screen keyer mode."

#define kParamSoftnessUpper "softnessUpper"
#define kParamSoftnessUpperLabel "Softness (upper)"
#define kParamSoftnessUpperHint "Width of the upper softness range [C, D] = [center+toleranceUpper, center+toleranceUpper+softnessUpper]. The key goes from 1 to 0 when the foreground key is over this range. Ignored in Screen keyer mode."

#define kParamDespill "despill"
#define kParamDespillLabel "Despill"
#define kParamDespillHint "Reduces color spill on the foreground object (Screen mode only). Between 0 and 1, only mixed foreground/background regions are despilled. Above 1, foreground regions are despilled too."

#define kParamDespillAngle "despillAngle"
#define kParamDespillAngleLabel "Despill Angle"
#define kParamDespillAngleHint "Opening of the cone centered around the keyColor where colors are despilled. A larger angle means that more colors are modified."
#define kParamDespillAngleDefault 120

#define kParamOutputMode "show"
#define kParamOutputModeLabel "Output Mode"
#define kParamOutputModeHint \
    "What image to output."
#define kParamOutputModeOptionIntermediate "Intermediate", "Color is the source color. Alpha is the foreground key. Use for multi-pass keying.", "intermediate"
#define kParamOutputModeOptionPremultiplied "Premultiplied", "Color is the Source color after key color suppression, multiplied by alpha. Alpha is the foreground key.", "premultiplied"
#define kParamOutputModeOptionUnpremultiplied "Unpremultiplied", "Color is the Source color after key color suppression. Alpha is the foreground key.", "unpremultiplied"
#define kParamOutputModeOptionComposite "Composite", "Color is the composite of Source and Bg. Alpha is the foreground key.", "composite"

#define kParamSourceAlpha "sourceAlphaHandling"
#define kParamSourceAlphaLabel "Source Alpha"
#define kParamSourceAlphaHint \
    "How the alpha embedded in the Source input should be used"
#define kParamSourceAlphaOptionIgnore "Ignore", "Ignore the source alpha.", "ignore"
#define kParamSourceAlphaOptionAddToInsideMask "Add to Inside Mask", "Source alpha is added to the inside mask. Use for multi-pass keying.", "inside"
#define kParamSourceAlphaOptionNormal "Normal", "Foreground key is multiplied by source alpha when compositing.", "normal"

#define kParamInvert "invert"
#define kParamInvertLabel "Invert"
#define kParamInvertHint \
    "Invert the matte computed by the keyer (alpha becomes 1 - alpha). In version 1, this gives the polarity of Foundry's Keyer node in Luminance and Color modes. " \
    "The Inside Mask and Outside Mask are applied after the inversion, and keep their meaning. The despill amount is that of the matte without Invert. Ignored in None keyer mode."

#define kClipSourceHint "The foreground image to key."
#define kClipBg "Bg"
#define kClipBgHint "The background image to replace the blue/green screen in the foreground."
#define kClipInsideMask "InM"
#define kClipInsideMaskHint "The Inside Mask, or holdout matte, or core matte, used to confirm areas that are definitely foreground."
#define kClipOutsidemask "OutM"
#define kClipOutsideMaskHint "The Outside Mask, or garbage matte, used to remove unwanted objects (lighting rigs, and so on) from the foreground. The Outside Mask has priority over the Inside Mask, so that areas where both are one are considered to be outside."

enum OutputModeEnum
{
    eOutputModeIntermediate,
    eOutputModePremultiplied,
    eOutputModeUnpremultiplied,
    eOutputModeComposite,
};

enum SourceAlphaEnum
{
    eSourceAlphaIgnore,
    eSourceAlphaAddToInsideMask,
    eSourceAlphaNormal,
};

class KeyerProcessorBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg;
    const Image *_bgImg;
    const Image *_inMaskImg;
    const Image *_outMaskImg;
    OfxRGBColourF _keyColor;
    KeyerModeEnum _keyerMode;
    LuminanceMathEnum _luminanceMath;
    double _softnessLower;
    double _toleranceLower;
    double _center;
    double _toleranceUpper;
    double _softnessUpper;
    double _despill;
    double _despillClosing;
    OutputModeEnum _outputMode;
    SourceAlphaEnum _sourceAlpha;
    bool _invert;
    bool _opaqueInRange = false; // version 2: opaque between B and C, except in Screen mode

public:

    KeyerProcessorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _srcImg(NULL)
        , _bgImg(NULL)
        , _inMaskImg(NULL)
        , _outMaskImg(NULL)
        , _keyerMode(eKeyerModeLuminance)
        , _luminanceMath(eLuminanceMathRec709)
        , _softnessLower(-0.5)
        , _toleranceLower(0.)
        , _center(0.)
        , _toleranceUpper(0.)
        , _softnessUpper(0.5)
        , _despill(0.)
        , _despillClosing(0.)
        , _outputMode(eOutputModeComposite)
        , _sourceAlpha(eSourceAlphaIgnore)
        , _invert(false)
    {
        _keyColor.r = _keyColor.g = _keyColor.b = 0.;
    }

    void setSrcImgs(const Image *srcImg,
                    const Image *bgImg,
                    const Image *inMaskImg,
                    const Image *outMaskImg)
    {
        _srcImg = srcImg;
        _bgImg = bgImg;
        _inMaskImg = inMaskImg;
        _outMaskImg = outMaskImg;
    }

    void setValues(const OfxRGBColourF& keyColor,
                   KeyerModeEnum keyerMode,
                   LuminanceMathEnum luminanceMath,
                   double softnessLower,
                   double toleranceLower,
                   double center,
                   double toleranceUpper,
                   double softnessUpper,
                   double despill,
                   double despillAngle,
                   OutputModeEnum outputMode,
                   SourceAlphaEnum sourceAlpha)
    {
        _keyColor = keyColor;
        _keyerMode = keyerMode;
        _luminanceMath = luminanceMath;
        _softnessLower = softnessLower;
        _toleranceLower = toleranceLower;
        _center = center;
        if (_keyerMode == eKeyerModeScreen) {
            _toleranceUpper = 1.;
            _softnessUpper = 1.;
            _despill = despill;
            _despillClosing = std::tan( (90 - 0.5 * despillAngle) * OFXS_PI / 180. );
        } else {
            _toleranceUpper = toleranceUpper;
            _softnessUpper = softnessUpper;
            _despill = (_keyerMode == eKeyerModeNone) ? despill : 0.;
            _despillClosing = (_keyerMode == eKeyerModeNone) ? std::tan( (90 - 0.5 * despillAngle) * OFXS_PI / 180. ) : 0.;
        }
        _outputMode = outputMode;
        _sourceAlpha = sourceAlpha;
    }

    // invert the key computed from the foreground key (not the masks, nor the despill amount)
    void setInvert(bool invert) { _invert = invert; }

    // version 2: the output alpha is the background key (opaque between B and C), except in Screen mode
    void setOpaqueInRange(bool opaqueInRange) { _opaqueInRange = opaqueInRange; }

    double key_bg(double Kfg)
    {
        if ( ( (_center + _toleranceLower) <= 0. ) && (Kfg <= 0.) ) { // special case: everything below 0 is 1. if center-toleranceLower<=0
            return 1.;
        } else if ( Kfg < (_center + _toleranceLower + _softnessLower) ) {
            return 0.;
        } else if ( ( Kfg < (_center + _toleranceLower) ) && (_softnessLower < 0.) ) {
            return ( Kfg - (_center + _toleranceLower + _softnessLower) ) / -_softnessLower;
        } else if (Kfg <= _center + _toleranceUpper) {
            return 1.;
        }  else if ( ( 1. <= (_center + _toleranceUpper) ) && (1. <= Kfg) ) {               // special case: everything above 1 is 1. if center+toleranceUpper>=1
            return 1.;
        } else if ( ( Kfg < (_center + _toleranceUpper + _softnessUpper) ) && (_softnessUpper > 0.) ) {
            return ( (_center + _toleranceUpper + _softnessUpper) - Kfg ) / _softnessUpper;
        } else {
            return 0.;
        }
    }

protected:
    // nonadditive mix of a background key with the inside and outside masks (in [0,1]):
    // the inside mask caps it at 1 - inMask, then the outside mask raises it to outMask
    static double applyMasks(double Kbg,
                             double inMask,
                             double outMask)
    {
        if ( (inMask > 0.) && (Kbg > 1. - inMask) ) {
            Kbg = 1. - inMask;
        }
        if ( (outMask > 0.) && (Kbg < outMask) ) {
            Kbg = outMask;
        }

        return Kbg;
    }

    double rgb2luminance(double r,
                         double g,
                         double b)
    {
        return luminance(r, g, b, _luminanceMath);
    }
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

// NaN rule (issues #195, #213): see ofxsNonFinite.h. clampToFloatRange is used on the input
// samples, so that the key and despill computations (in double) never see inf * 0 or inf - inf,
// and on the double results converted to float.

template <class PIX, int nComponents, int maxValue>
class KeyerProcessor
    : public KeyerProcessorBase
{
public:
    KeyerProcessor(ImageEffect &instance)
        : KeyerProcessorBase(instance)
    {
    }

private:
    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        unused(rs);
        // the Source may be RGB (in the general context) while the Output is RGBA
        const bool srcHasAlpha = _srcImg && (_srcImg->getPixelComponentCount() == 4);
        // for Color and Screen modes, how much the scalar product between RGB and the keyColor must be
        // multiplied by to get the foreground key value 1, which corresponds to the maximum
        // possible value, e.g. for (R,G,B)=(1,1,1)
        // Kfg = 1 = colorKeyFactor * (1,1,1)._keyColor (where "." is the scalar product)
        const float keyColor111 = _keyColor.r + _keyColor.g + _keyColor.b;
        // const double keyColorFactor = (keyColor111 == 0.) ? 1. : 1./keyColor111;
        // squared norm of keyColor, and norm, used for Screen mode.
        // They are computed in float, unless the float squared norm overflows to inf (a key colour
        // component beyond about 1.8e19, issue #213: d was then |fg| in Screen mode, and the despill
        // was skipped): then in double, where it can't (the key colour is in the float range).
        const float keyColorNorm2F = (_keyColor.r * _keyColor.r) + (_keyColor.g * _keyColor.g) + (_keyColor.b * _keyColor.b);
        const bool keyColorNorm2InFloat = (keyColorNorm2F <= FLT_MAX);
        const double keyColorNorm2 = keyColorNorm2InFloat ? (double)keyColorNorm2F :
                                     ( (double)_keyColor.r * _keyColor.r + (double)_keyColor.g * _keyColor.g + (double)_keyColor.b * _keyColor.b );
        const double keyColorNorm = keyColorNorm2InFloat ? (double)std::sqrt(keyColorNorm2F) : std::sqrt(keyColorNorm2);

        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            assert(dstPix);

            for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                const PIX *bgPix = (const PIX *)  (_bgImg ? _bgImg->getPixelAddress(x, y) : 0);
                const PIX *inMaskPix = (const PIX *)  (_inMaskImg ? _inMaskImg->getPixelAddress(x, y) : 0);
                const PIX *outMaskPix = (const PIX *)  (_outMaskImg ? _outMaskImg->getPixelAddress(x, y) : 0);
                float inMask = inMaskPix ? sampleToFloat<PIX, maxValue>(*inMaskPix) : 0.f;
                if ( (_sourceAlpha == eSourceAlphaAddToInsideMask) && srcPix ) {
                    // take the max of inMask and the source Alpha (an RGB Source is opaque, as when the
                    // host maps it to RGBA: srcPix[3] would be the next pixel's red)
                    inMask = (std::max)( inMask, srcHasAlpha ? clampToFloatRange( sampleToFloat<PIX, maxValue>(srcPix[3]) ) : 1.f );
                }
                float outMask = outMaskPix ? sampleToFloat<PIX, maxValue>(*outMaskPix) : 0.f;
                double Kbg = 0.;

                // clamp inMask and outMask in the [0,1] range (NaN gives 0)
                inMask = clampToUnit(inMask);
                outMask = clampToUnit(outMask);

                // output of the foreground suppressor
                // non-finite samples: NaN gives 0, +-inf gives +-FLT_MAX (see clampToFloatRange)
                double fgr = srcPix ? clampToFloatRange( sampleToFloat<PIX, maxValue>(srcPix[0]) ) : 0.;
                double fgg = srcPix ? clampToFloatRange( sampleToFloat<PIX, maxValue>(srcPix[1]) ) : 0.;
                double fgb = srcPix ? clampToFloatRange( sampleToFloat<PIX, maxValue>(srcPix[2]) ) : 0.;
                double bgr = bgPix ? clampToFloatRange( sampleToFloat<PIX, maxValue>(bgPix[0]) ) : 0.;
                double bgg = bgPix ? clampToFloatRange( sampleToFloat<PIX, maxValue>(bgPix[1]) ) : 0.;
                double bgb = bgPix ? clampToFloatRange( sampleToFloat<PIX, maxValue>(bgPix[2]) ) : 0.;

                // we want to be able to play with the matte even if the background is not connected
                if (!srcPix) {
                    // no source, take only background
                    Kbg = 1.;
                    fgr = fgg = fgb = 0.;
                } else if (outMask >= 1.) { // optimize
                    Kbg = 1.;
                    fgr = fgg = fgb = 0.;
                } else {
                    // from fgr, fgg, fgb, compute Kbg and update fgr, fgg, fgb

                    double Kfg;
                    double scalarProd = 0.;
                    double norm2 = 0.; // squared norm of fg
                    // d is the norm of projection of fg orthogonal to keyColor.
                    // It is norm(fg) if fg is orthogonal to keyColor, and zero if
                    // fg is in the direction of keycolor
                    double d = 0.;
                    switch (_keyerMode) {
                    case eKeyerModeLuminance: {
                        Kfg = rgb2luminance(fgr, fgg, fgb);
                        break;
                    }
                    case eKeyerModeColor: {
                        scalarProd = fgr * _keyColor.r + fgg * _keyColor.g + fgb * _keyColor.b;
                        Kfg = (keyColor111 == 0) ? rgb2luminance(fgr, fgg, fgb) : (scalarProd / keyColor111);
                        break;
                    }
                    case eKeyerModeScreen: {
                        scalarProd = fgr * _keyColor.r + fgg * _keyColor.g + fgb * _keyColor.b;
                        norm2 = fgr * fgr + fgg * fgg + fgb * fgb;
                        d = std::sqrt( (std::max)( 0., norm2 - ( (keyColorNorm2 == 0) ? 0. : (scalarProd * scalarProd / keyColorNorm2) ) ) );
                        Kfg = (keyColor111 == 0) ? rgb2luminance(fgr, fgg, fgb) : (scalarProd / keyColor111);
                        Kfg -= d;
                        break;
                    }
                    case eKeyerModeNone: {
                        scalarProd = fgr * _keyColor.r + fgg * _keyColor.g + fgb * _keyColor.b;
                        norm2 = fgr * fgr + fgg * fgg + fgb * fgb;
                        d = std::sqrt( (std::max)( 0., norm2 - ( (keyColorNorm2 == 0) ? 0. : (scalarProd * scalarProd / keyColorNorm2) ) ) );
                        break;
                    }
                    case eKeyerModeSaturation:
                    case eKeyerModeMin:
                    case eKeyerModeMax: {
                        Kfg = rgbKeyValue(_keyerMode, fgr, fgg, fgb);
                        break;
                    }
                    }

                    // compute Kbg from Kfg
                    if (_keyerMode == eKeyerModeNone) {
                        Kbg = 1.;
                    } else {
                        // in [0,1] (NaN would give 0, but Kfg is finite: the samples are)
                        Kbg = clampToUnit( key_bg(Kfg) );
                    }
                    // The despill amount uses the key without Invert: the spill comes from the key
                    // color, whatever the polarity of the output matte.
                    double KbgDespill = Kbg;
                    // the background key is 1 - alpha: in version 1 (and in Screen mode), alpha is 1 - key,
                    // so that the key color is transparent; in version 2, alpha is the key (opaque between
                    // B and C) in the other modes. Invert inverts this.
                    const bool invertKey = ( _invert != (_opaqueInRange && (_keyerMode != eKeyerModeScreen) ) );
                    if (invertKey && (_keyerMode != eKeyerModeNone) ) {
                        // invert the key, not the masks, which are applied below
                        Kbg = 1. - Kbg;
                    }
                    // nonadditive mix between the key generator and the garbage matte (outMask)
                    // note that in Chromakeyer this is done before on Kfg instead of Kbg.
                    Kbg = applyMasks(Kbg, inMask, outMask);
                    KbgDespill = applyMasks(KbgDespill, inMask, outMask);


                    // despill fgr, fgg, fgb
                    if ( (_despill > 0.) && ( (_keyerMode == eKeyerModeNone) || (_keyerMode == eKeyerModeScreen) ) && (_outputMode != eOutputModeIntermediate) && (keyColorNorm2 > 0.) ) {
                        // color in the direction of keyColor
                        if (scalarProd / keyColorNorm > d * _despillClosing) {
                            // maxdespill is between 0 and 1:
                            // if despill in [0,1]: only outside regions are despilled
                            // if despill in [1,2]: inside regions are despilled too
                            assert(0 <= KbgDespill && KbgDespill <= 1);
                            assert(0 <= _despill && _despill <= 2);
                            double maxdespill = KbgDespill * (std::min)(_despill, 1.) + (1 - KbgDespill) * (std::max)(0., _despill - 1);
                            assert(0 <= maxdespill && maxdespill <= 1);

                            //// first solution: despill proportionally to the distance to the the despill cone
                            //// in the direction on -_keyColor
                            //double colorshift = maxdespill*(scalarProd/keyColorNorm - d * _despillClosing);

                            // second solution: subtract maxdespill * _keyColor, clamping to the despill cone
                            double colorshift = maxdespill * (std::max)( keyColorNorm, (scalarProd / keyColorNorm - d * _despillClosing) );
                            // clamp: don't go beyond the despill cone
                            colorshift = (std::min)(colorshift, scalarProd / keyColorNorm - d * _despillClosing);
                            assert(colorshift >= 0);
                            fgr -= colorshift * _keyColor.r / keyColorNorm;
                            fgg -= colorshift * _keyColor.g / keyColorNorm;
                            fgb -= colorshift * _keyColor.b / keyColorNorm;
                        }
                    }

                    // premultiply foreground
                    if (_outputMode != eOutputModeUnpremultiplied) {
                        fgr *= (1. - Kbg);
                        fgg *= (1. - Kbg);
                        fgb *= (1. - Kbg);
                    }

                    // clamp foreground color to [0,1]
                    fgr = (std::max)( 0., (std::min)(fgr, 1.) );
                    fgg = (std::max)( 0., (std::min)(fgg, 1.) );
                    fgb = (std::max)( 0., (std::min)(fgb, 1.) );
                }

                // At this point, we have Kbg,

                // set the alpha channel to the complement of Kbg
                double fga = 1. - Kbg;
                //double fga = Kbg;
                assert(fga >= 0. && fga <= 1.);
                double compAlpha = (_outputMode == eOutputModeComposite &&
                                    _sourceAlpha == eSourceAlphaNormal &&
                                    srcHasAlpha && srcPix) ? clampToFloatRange( sampleToFloat<PIX, maxValue>(srcPix[3]) ) : 1.;
                switch (_outputMode) {
                case eOutputModeIntermediate:
                    for (int c = 0; c < 3; ++c) {
                        // the source, with non-finite float samples made finite (integer samples are copied)
                        dstPix[c] = srcPix ? finiteSample<PIX, maxValue>(srcPix[c]) : 0;
                    }
                    break;
                case eOutputModePremultiplied:
                case eOutputModeUnpremultiplied:
                    dstPix[0] = (float)floatToSample<PIX, maxValue>(fgr);
                    dstPix[1] = (float)floatToSample<PIX, maxValue>(fgg);
                    dstPix[2] = (float)floatToSample<PIX, maxValue>(fgb);
                    break;
                case eOutputModeComposite:
                    // [FD] not sure if this is the expected way to use compAlpha
                    // (clamped to the float range: a huge Bg or source alpha may overflow it)
                    dstPix[0] = (float)floatToSample<PIX, maxValue>( clampToFloatRange(compAlpha * (fgr + bgr * Kbg) + (1. - compAlpha) * bgr) );
                    dstPix[1] = (float)floatToSample<PIX, maxValue>( clampToFloatRange(compAlpha * (fgg + bgg * Kbg) + (1. - compAlpha) * bgg) );
                    dstPix[2] = (float)floatToSample<PIX, maxValue>( clampToFloatRange(compAlpha * (fgb + bgb * Kbg) + (1. - compAlpha) * bgb) );
                    break;
                }
                if (nComponents == 4) {
                    dstPix[3] = floatToSample<PIX, maxValue>(fga);
                }
            }
        }
    } // multiThreadProcessImages
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class KeyerPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    KeyerPlugin(OfxImageEffectHandle handle,
                unsigned int majorVersion)
        : ImageEffect(handle)
        , _majorVersion(majorVersion)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _bgClip(NULL)
        , _inMaskClip(NULL)
        , _outMaskClip(NULL)
        , _sublabel(NULL)
        , _keyColor(NULL)
        , _keyerMode(NULL)
        , _luminanceMath(NULL)
        , _softnessLower(NULL)
        , _toleranceLower(NULL)
        , _center(NULL)
        , _toleranceUpper(NULL)
        , _softnessUpper(NULL)
        , _despill(NULL)
        , _despillAngle(NULL)
        , _outputMode(NULL)
        , _sourceAlpha(NULL)
        , _invert(NULL)
    {

        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA) ) );
        _bgClip = fetchClip(kClipBg);
        assert( _bgClip && (!_bgClip->isConnected() || _bgClip->getPixelComponents() == ePixelComponentRGB || _bgClip->getPixelComponents() == ePixelComponentRGBA) );
        _inMaskClip = fetchClip(kClipInsideMask);;
        assert( _inMaskClip && (!_inMaskClip->isConnected() || _inMaskClip->getPixelComponents() == ePixelComponentAlpha) );
        _outMaskClip = fetchClip(kClipOutsidemask);;
        assert( _outMaskClip && (!_outMaskClip->isConnected() || _outMaskClip->getPixelComponents() == ePixelComponentAlpha) );
        _sublabel = fetchStringParam(kNatronOfxParamStringSublabelName);
        _keyColor = fetchRGBParam(kParamKeyColor);
        _keyerMode = fetchChoiceParam(kParamKeyerMode);
        _luminanceMath = fetchChoiceParam(kParamLuminanceMath);
        _softnessLower = fetchDoubleParam(kParamSoftnessLower);
        _toleranceLower = fetchDoubleParam(kParamToleranceLower);
        _center = fetchDoubleParam(kParamCenter);
        _toleranceUpper = fetchDoubleParam(kParamToleranceUpper);
        _softnessUpper = fetchDoubleParam(kParamSoftnessUpper);
        _despill = fetchDoubleParam(kParamDespill);
        _despillAngle = fetchDoubleParam(kParamDespillAngle);
        assert(_keyColor && _keyerMode && _luminanceMath && _softnessLower && _toleranceLower && _center && _toleranceUpper && _softnessUpper && _despill && _despillAngle);
        _outputMode = fetchChoiceParam(kParamOutputMode);
        _sourceAlpha = fetchChoiceParam(kParamSourceAlpha);
        _invert = fetchBooleanParam(kParamInvert);
        assert(_outputMode && _sourceAlpha && _invert);

        // finally
        syncPrivateData();
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /** @brief get the clip preferences */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(KeyerProcessorBase &, const RenderArguments &args);

    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /** @brief The sync private data action, called when the effect needs to sync any private data to persistent parameters */
    virtual void syncPrivateData(void) OVERRIDE FINAL
    {
        updateVisibility();
    }

    void setThresholdsFromKeyColor(double r, double g, double b, KeyerModeEnum keyerMode, LuminanceMathEnum luminanceMath);

    void updateVisibility()
    {
        KeyerModeEnum keyerMode = (KeyerModeEnum)_keyerMode->getValue();

        _luminanceMath->setIsSecretAndDisabled(keyerMode != eKeyerModeLuminance);
        _softnessLower->setIsSecretAndDisabled(keyerMode == eKeyerModeNone);
        _toleranceLower->setIsSecretAndDisabled(keyerMode == eKeyerModeNone);
        _center->setIsSecretAndDisabled(keyerMode == eKeyerModeNone);
        _toleranceUpper->setIsSecretAndDisabled(keyerMode == eKeyerModeNone || keyerMode == eKeyerModeScreen);
        _softnessUpper->setIsSecretAndDisabled(keyerMode == eKeyerModeNone || keyerMode == eKeyerModeScreen);
        _despill->setIsSecretAndDisabled( !(keyerMode == eKeyerModeNone || keyerMode == eKeyerModeScreen) );
        _despillAngle->setIsSecretAndDisabled( !(keyerMode == eKeyerModeNone || keyerMode == eKeyerModeScreen) );
        _invert->setIsSecretAndDisabled(keyerMode == eKeyerModeNone);

        std::string keyerModeString;
        _keyerMode->getOption( (int)keyerMode, keyerModeString );
        _sublabel->setValue(keyerModeString);
    }

private:
    const unsigned int _majorVersion;
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    Clip *_bgClip;
    Clip *_inMaskClip;
    Clip *_outMaskClip;
    StringParam *_sublabel;
    RGBParam* _keyColor;
    ChoiceParam* _keyerMode;
    ChoiceParam* _luminanceMath;
    DoubleParam* _softnessLower;
    DoubleParam* _toleranceLower;
    DoubleParam* _center;
    DoubleParam* _toleranceUpper;
    DoubleParam* _softnessUpper;
    DoubleParam* _despill;
    DoubleParam* _despillAngle;
    ChoiceParam* _outputMode;
    ChoiceParam* _sourceAlpha;
    BooleanParam* _invert;
};

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
KeyerPlugin::setupAndProcess(KeyerProcessorBase &processor,
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
# ifndef NDEBUG
    checkBadRenderScaleOrField(dst, args);
# endif
    auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                    _srcClip->fetchImage(time) : 0 );
    auto_ptr<const Image> bg( ( _bgClip && _bgClip->isConnected() ) ?
                                   _bgClip->fetchImage(time) : 0 );
    if ( src.get() ) {
# ifndef NDEBUG
        checkBadRenderScaleOrField(src, args);
# endif
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        //PixelComponentEnum srcComponents = src->getPixelComponents();
        if (srcBitDepth != dstBitDepth /* || srcComponents != dstComponents*/) { // Keyer outputs RGBA but may have RGB input
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    if ( bg.get() ) {
# ifndef NDEBUG
        checkBadRenderScaleOrField(bg, args);
# endif
        BitDepthEnum srcBitDepth      = bg->getPixelDepth();
        //PixelComponentEnum srcComponents = bg->getPixelComponents();
        if (srcBitDepth != dstBitDepth /* || srcComponents != dstComponents*/) {  // Keyer outputs RGBA but may have RGB input
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    // auto ptr for the masks.
    auto_ptr<const Image> inMask( ( _inMaskClip && _inMaskClip->isConnected() ) ?
                                       _inMaskClip->fetchImage(time) : 0 );
    if ( inMask.get() ) {
        checkBadRenderScaleOrField(inMask, args);
    }
    auto_ptr<const Image> outMask( ( _outMaskClip && _outMaskClip->isConnected() ) ?
                                        _outMaskClip->fetchImage(time) : 0 );
    if ( outMask.get() ) {
        checkBadRenderScaleOrField(outMask, args);
    }

    double keyColorr, keyColorg, keyColorb;
    _keyColor->getValueAtTime(time, keyColorr, keyColorg, keyColorb);
    OfxRGBColourF keyColorF;
    // in the float range (converting a double beyond it to float is undefined; NaN gives 0)
    keyColorF.r = static_cast<float>( clampToFloatRange(keyColorr) );
    keyColorF.g = static_cast<float>( clampToFloatRange(keyColorg) );
    keyColorF.b = static_cast<float>( clampToFloatRange(keyColorb) );
    KeyerModeEnum keyerMode = (KeyerModeEnum)_keyerMode->getValueAtTime(time);
    LuminanceMathEnum luminanceMath = (LuminanceMathEnum)_luminanceMath->getValueAtTime(time);
    double softnessLower = _softnessLower->getValueAtTime(time);
    double toleranceLower = _toleranceLower->getValueAtTime(time);
    double center = _center->getValueAtTime(time);
    double toleranceUpper = _toleranceUpper->getValueAtTime(time);
    double softnessUpper = _softnessUpper->getValueAtTime(time);
    double despill = _despill->getValueAtTime(time);
    double despillAngle = _despillAngle->getValueAtTime(time);
    OutputModeEnum outputMode = (OutputModeEnum)_outputMode->getValueAtTime(time);
    SourceAlphaEnum sourceAlpha = (SourceAlphaEnum)_sourceAlpha->getValueAtTime(time);
    processor.setValues(keyColorF, keyerMode, luminanceMath, softnessLower, toleranceLower, center, toleranceUpper, softnessUpper, despill, despillAngle, outputMode, sourceAlpha);
    processor.setInvert( _invert->getValueAtTime(time) );
    processor.setOpaqueInRange(_majorVersion >= 2);
    processor.setDstImg( dst.get() );
    processor.setSrcImgs( src.get(), bg.get(), inMask.get(), outMask.get() );
    processor.setRenderWindow(args.renderWindow, args.renderScale);

    processor.process();
} // KeyerPlugin::setupAndProcess

// the overridden render function
void
KeyerPlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    if (dstComponents != ePixelComponentRGBA) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host dit not take into account output components");
        throwSuiteStatusException(kOfxStatErrImageFormat);

        return;
    }

    switch (dstBitDepth) {
    //case eBitDepthUByte: {
    //    KeyerProcessor<unsigned char, 4, 255> fred(*this);
    //    setupAndProcess(fred, args);
    //    break;
    //}
    case eBitDepthUShort: {
        KeyerProcessor<unsigned short, 4, 65535> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthFloat: {
        KeyerProcessor<float, 4, 1> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

/* Override the clip preferences */
void
KeyerPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    // set the premultiplication of _dstClip
    OutputModeEnum outputMode = (OutputModeEnum)_outputMode->getValue();

    switch (outputMode) {
    case eOutputModeIntermediate:
    case eOutputModeUnpremultiplied:
    case eOutputModeComposite:
        clipPreferences.setOutputPremultiplication(eImageUnPreMultiplied);
        break;
    case eOutputModePremultiplied:
        clipPreferences.setOutputPremultiplication(eImagePreMultiplied);
        break;
    }

    // Output is RGBA. Only the general context may set it: in the filter context, "a plugin cannot
    // change the component type of the Output clip, it will always be the same as the Source clip"
    // (ofxImageEffectContexts.rst, The Filter Context), and Source is RGBA there (see describeInContext).
    // note: the processor handles inputs with different components (an RGB input is opaque)
    if (getContext() == eContextGeneral) {
        clipPreferences.setClipComponents(*_dstClip, ePixelComponentRGBA);
    }
}

void
KeyerPlugin::setThresholdsFromKeyColor(double r,
                                       double g,
                                       double b,
                                       KeyerModeEnum keyerMode,
                                       LuminanceMathEnum luminanceMath)
{
    // l: the foreground key of the key color
    double l = 0.;
    switch (keyerMode) {
    case eKeyerModeNone:
        return;
    case eKeyerModeLuminance:
        l = luminance(r, g, b, luminanceMath);
        break;
    case eKeyerModeSaturation:
    case eKeyerModeMin:
    case eKeyerModeMax:
        l = rgbKeyValue(keyerMode, r, g, b);
        break;
    case eKeyerModeColor:
    case eKeyerModeScreen: {
        // for Color and Screen modes, how much the scalar product between RGB and the keyColor must be
        // multiplied by to get the foreground key value 1, which corresponds to the maximum
        // possible value, e.g. for (R,G,B)=(1,1,1)
        // Kfg = 1 = colorKeyFactor * (1,1,1)._keyColor (where "." is the scalar product)
        const double keyColor111 = r + g + b;
        const double keyColorNorm2 = (r * r) + (g * g) + (b * b);
        l = keyColor111 == 0. ? 0. : (keyColorNorm2 / keyColor111);
        break;
    }
    }
    if (_majorVersion < 2) {
        // version 1: the key is 1 at the key color (transparent), 0 at 0 and 1
        _softnessLower->setValue(-l);
        _toleranceLower->setValue(0.);
        _center->setValue(l);
        _toleranceUpper->setValue(0.);
        _softnessUpper->setValue(1. - l);

        return;
    }
    // version 2: in [0, 1], so that the parameters stay in their ranges (HDR or negative key colors)
    l = (std::min)( (std::max)(l, 0.), 1. );
    if (keyerMode == eKeyerModeScreen) {
        // alpha is 1 minus the key: the key is 1 at the key color (transparent), as in version 1
        _softnessLower->setValue(-l);
        _toleranceLower->setValue(0.);
        _center->setValue(l);
        _toleranceUpper->setValue(0.);
        _softnessUpper->setValue(1. - l);
    } else {
        // alpha is the key: A = l (transparent at the key color), B = C = D = 1
        _softnessLower->setValue(l - 1.);
        _toleranceLower->setValue(0.);
        _center->setValue(1.);
        _toleranceUpper->setValue(0.);
        _softnessUpper->setValue(0.);
    }
}

void
KeyerPlugin::changedParam(const InstanceChangedArgs &args,
                          const std::string &paramName)
{
    const double time = args.time;

    if ( (paramName == kParamKeyColor) && (args.reason == eChangeUserEdit) ) {
        OfxRGBColourD keyColor;
        _keyColor->getValueAtTime(time, keyColor.r, keyColor.g, keyColor.b);
        KeyerModeEnum keyerMode = (KeyerModeEnum)_keyerMode->getValueAtTime(time);
        LuminanceMathEnum luminanceMath = (keyerMode == eKeyerModeLuminance) ? (LuminanceMathEnum)_luminanceMath->getValueAtTime(time) : eLuminanceMathRec709;
        setThresholdsFromKeyColor(keyColor.r, keyColor.g, keyColor.b, keyerMode, luminanceMath);
    }
    if ( (paramName == kParamKeyerMode) && (args.reason == eChangeUserEdit) ) {
        updateVisibility();

        KeyerModeEnum keyerMode = (KeyerModeEnum)_keyerMode->getValueAtTime(time);
        // version 1 sets A, B, C, D from the key color when the mode changes; version 2 keeps them, as the
        // Keyer node does (issue #280; only an edit of the key color sets them)
        if (_majorVersion < 2) {
            OfxRGBColourD keyColor;
            _keyColor->getValueAtTime(time, keyColor.r, keyColor.g, keyColor.b);
            LuminanceMathEnum luminanceMath = (keyerMode == eKeyerModeLuminance) ? (LuminanceMathEnum)_luminanceMath->getValueAtTime(time) : eLuminanceMathRec709;
            setThresholdsFromKeyColor(keyColor.r, keyColor.g, keyColor.b, keyerMode, luminanceMath);
        }
        std::string keyerModeString;
        _keyerMode->getOption( (int)keyerMode, keyerModeString );
        _sublabel->setValue(keyerModeString);
    }
}

mDeclarePluginFactoryVersioned(KeyerPluginFactory, {ofxsThreadSuiteCheck();}, {});
template<unsigned int majorVersion>
void
KeyerPluginFactory<majorVersion>::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(majorVersion < 2 ? kPluginDescriptionV1 : kPluginDescription);
    if (majorVersion < kPluginVersionMajor) {
        desc.setIsObsolete(true);
    }

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    //desc.addSupportedBitDepth(eBitDepthUByte);
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
}

template<unsigned int majorVersion>
void
KeyerPluginFactory<majorVersion>::describeInContext(ImageEffectDescriptor &desc,
                                      ContextEnum context)
{
    ClipDescriptor* srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->setHint(kClipSourceHint);
    srcClip->addSupportedComponent( ePixelComponentRGBA );
    if (context == eContextGeneral) {
        // The Output is RGBA (it holds the matte). In the other contexts (filter), "a plugin cannot
        // change the component type of the Output clip, it will always be the same as the Source
        // clip" (ofxImageEffectContexts.rst, The Filter Context): Source is RGBA there, and the host
        // maps an RGB input to RGBA ("it must map the clip to RGBA in a manner that is transparent
        // to the plugin", ofxClipPreferences.rst).
        srcClip->addSupportedComponent( ePixelComponentRGB );
    }
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setOptional(false);

    // create the inside mask clip
    ClipDescriptor *inMaskClip =  desc.defineClip(kClipInsideMask);
    inMaskClip->setHint(kClipInsideMaskHint);
    inMaskClip->addSupportedComponent(ePixelComponentAlpha);
    inMaskClip->setTemporalClipAccess(false);
    inMaskClip->setOptional(true);
    inMaskClip->setSupportsTiles(kSupportsTiles);
    inMaskClip->setIsMask(true);

    // outside mask clip (garbage matte)
    ClipDescriptor *outMaskClip =  desc.defineClip(kClipOutsidemask);
    outMaskClip->setHint(kClipOutsideMaskHint);
    outMaskClip->addSupportedComponent(ePixelComponentAlpha);
    outMaskClip->setTemporalClipAccess(false);
    outMaskClip->setOptional(true);
    outMaskClip->setSupportsTiles(kSupportsTiles);
    outMaskClip->setIsMask(true);

    ClipDescriptor* bgClip = desc.defineClip(kClipBg);
    bgClip->setHint(kClipBgHint);
    bgClip->addSupportedComponent( ePixelComponentRGBA );
    bgClip->addSupportedComponent( ePixelComponentRGB );
    bgClip->setTemporalClipAccess(false);
    bgClip->setSupportsTiles(kSupportsTiles);
    bgClip->setOptional(true);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->setSupportsTiles(kSupportsTiles);


    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // sublabel
    {
        StringParamDescriptor* param = desc.defineStringParam(kNatronOfxParamStringSublabelName);
        param->setIsSecretAndDisabled(true); // always secret
        param->setIsPersistent(false);
        param->setEvaluateOnChange(false);
        std::string keyerModeString;
        param->setDefault(kParamKeyerModeDefaultString);
        if (page) {
            page->addChild(*param);
        }
    }

    // key color
    {
        RGBParamDescriptor* param = desc.defineRGBParam(kParamKeyColor);
        param->setLabel(kParamKeyColorLabel);
        param->setHint(kParamKeyColorHint);
        param->setDefault(0., 0., 0.);
        // the following should be the default
        double kmin = -std::numeric_limits<double>::max();
        double kmax = std::numeric_limits<double>::max();
        param->setRange(kmin, kmin, kmin, kmax, kmax, kmax);
        param->setDisplayRange(0., 0., 0., 1., 1., 1.);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // invert (added in version 1.1, issue #280; off, so that existing projects render the same), before the keyer mode (#380)
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamInvert);
        param->setLabel(kParamInvertLabel);
        param->setHint(kParamInvertHint);
        param->setDefault(false);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // keyer mode
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamKeyerMode);
        param->setLabel(kParamKeyerModeLabel);
        param->setHint(kParamKeyerModeHint);
        assert(param->getNOptions() == (int)eKeyerModeLuminance);
        param->appendOption(kParamKeyerModeOptionLuminance);
        assert(param->getNOptions() == (int)eKeyerModeColor);
        param->appendOption(kParamKeyerModeOptionColor);
        assert(param->getNOptions() == (int)eKeyerModeScreen);
        param->appendOption(kParamKeyerModeOptionScreen);
        assert(param->getNOptions() == (int)eKeyerModeNone);
        param->appendOption(kParamKeyerModeOptionNone);
        if (majorVersion >= 2) {
            assert(param->getNOptions() == (int)eKeyerModeSaturation);
            param->appendOption(kParamKeyerModeOptionSaturation);
            assert(param->getNOptions() == (int)eKeyerModeMin);
            param->appendOption(kParamKeyerModeOptionMin);
            assert(param->getNOptions() == (int)eKeyerModeMax);
            param->appendOption(kParamKeyerModeOptionMax);
        }
        param->setDefault( (int)kParamKeyerModeDefault );
        if (page) {
            page->addChild(*param);
        }
    }
    // luminance math
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

    // softness (lower)
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSoftnessLower);
        param->setLabel(kParamSoftnessLowerLabel);
        param->setHint(kParamSoftnessLowerHint);
        param->setRange(-1., 0.);
        param->setDisplayRange(-1., 0.);
        param->setDigits(5);
        // A = 0 in version 2 (A, B, C, D = 0, 1, 1, 1, as in Foundry's Keyer node, issue #280), 0.5 in version 1
        param->setDefault(majorVersion < 2 ? -0.5 : -1.);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // tolerance (lower)
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamToleranceLower);
        param->setLabel(kParamToleranceLowerLabel);
        param->setHint(kParamToleranceLowerHint);
        param->setRange(-1., 0.);
        param->setDisplayRange(-1., 0.);
        param->setDigits(5);
        param->setDefault(0.);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // center
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamCenter);
        param->setLabel(kParamCenterLabel);
        param->setHint(kParamCenterHint);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        param->setDigits(5);
        param->setDefault(1.);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // tolerance (upper)
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamToleranceUpper);
        param->setLabel(kParamToleranceUpperLabel);
        param->setHint(kParamToleranceUpperHint);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        param->setDigits(5);
        param->setDefault(0.);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // softness (upper)
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSoftnessUpper);
        param->setLabel(kParamSoftnessUpperLabel);
        param->setHint(kParamSoftnessUpperHint);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        param->setDigits(5);
        // D = C in version 2 (A, B, C, D = 0, 1, 1, 1), C + 0.5 in version 1
        param->setDefault(majorVersion < 2 ? 0.5 : 0.);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // despill
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamDespill);
        param->setLabel(kParamDespillLabel);
        param->setHint(kParamDespillHint);
        param->setRange(0., 2.);
        param->setDisplayRange(0., 2.);
        param->setDefault(1.);
        //param->setEnabled((kParamKeyerModeDefault == eKeyerModeScreen) || (kParamKeyerModeDefault == eKeyerModeNone));
        if (page) {
            page->addChild(*param);
        }
    }

    // despillAngle
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamDespillAngle);
        param->setLabel(kParamDespillAngleLabel);
        param->setHint(kParamDespillAngleHint);
        param->setRange(0., 180.);
        param->setDisplayRange(0., 180.);
        param->setDefault(120.);
        //param->setEnabled((kParamKeyerModeDefault == eKeyerModeScreen) || (kParamKeyerModeDefault == eKeyerModeNone));
        if (page) {
            page->addChild(*param);
        }
    }

    // output mode
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOutputMode);
        param->setLabel(kParamOutputModeLabel);
        param->setHint(kParamOutputModeHint);
        assert(param->getNOptions() == (int)eOutputModeIntermediate);
        param->appendOption(kParamOutputModeOptionIntermediate);
        assert(param->getNOptions() == (int)eOutputModePremultiplied);
        param->appendOption(kParamOutputModeOptionPremultiplied);
        assert(param->getNOptions() == (int)eOutputModeUnpremultiplied);
        param->appendOption(kParamOutputModeOptionUnpremultiplied);
        assert(param->getNOptions() == (int)eOutputModeComposite);
        param->appendOption(kParamOutputModeOptionComposite);
        param->setDefault( (int)eOutputModeIntermediate );
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }

    // source alpha
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamSourceAlpha);
        param->setLabel(kParamSourceAlphaLabel);
        param->setHint(kParamSourceAlphaHint);
        assert(param->getNOptions() == (int)eSourceAlphaIgnore);
        param->appendOption(kParamSourceAlphaOptionIgnore);
        assert(param->getNOptions() == (int)eSourceAlphaAddToInsideMask);
        param->appendOption(kParamSourceAlphaOptionAddToInsideMask);
        assert(param->getNOptions() == (int)eSourceAlphaNormal);
        param->appendOption(kParamSourceAlphaOptionNormal);
        param->setDefault( (int)eSourceAlphaIgnore );
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
} // KeyerPluginFactory::describeInContext

template<unsigned int majorVersion>
ImageEffect*
KeyerPluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                                 ContextEnum /*context*/)
{
    return new KeyerPlugin(handle, majorVersion);
}

static KeyerPluginFactory<kPluginVersionMajor> p(kPluginIdentifier, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)
static KeyerPluginFactory<1> p1(kPluginIdentifier, kPluginVersionMinorV1);
mRegisterPluginFactoryInstance(p1)

OFXS_NAMESPACE_ANONYMOUS_EXIT
