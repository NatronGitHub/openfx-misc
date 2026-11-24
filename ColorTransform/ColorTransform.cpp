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
 * OFX ColorTransform plugin.
 */

#include <cmath>

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"
#include "ofxsLut.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

// The hue of HSV, HSL and HSI is a fraction of the hue circle (OFXS_HUE_CIRCLE in SupportExt's ofxsLut.h).
#define kHueNote "For RGB in [0,1], H is in [0,1] (a fraction of the hue circle, not degrees; it can round to 1): red is 0, yellow 1/6, green 1/3, cyan 1/2, blue 2/3 and magenta 5/6."
#define kAsGivenRGBToNote \
    "The conversion uses the RGB values as they are given: no transfer function (gamma) is applied. " \
    "HSV and HSL are usually computed on display-referred values, e.g. sRGB-encoded ones: " \
    "on linear (scene-referred) values, the result has other hues and saturations. " \
    "To get the display-referred values, convert the input to the display colour space first (and back after the inverse conversion)."
#define kAsGivenToRGBNote \
    "The output RGB values are given as computed: no transfer function (gamma) is applied, " \
    "so they are in the colour encoding of the input of the forward conversion."

#define kPluginRGBToHSVName "RGBToHSV"
#define kPluginRGBToHSVDescription "Convert from RGB to the HSV color model (hue, saturation, value, as defined by A. R. Smith in 1978).\n" \
    kHueNote " V = max(R,G,B), in the units of RGB. S = (max(R,G,B) - min(R,G,B)) / max(R,G,B), in [0,1] for non-negative RGB (0 if V is 0).\n" \
    kAsGivenRGBToNote
#define kPluginRGBToHSVIdentifier "net.sf.openfx.RGBToHSV"

#define kPluginHSVToRGBName "HSVToRGB"
#define kPluginHSVToRGBDescription "Convert from the HSV color model (hue, saturation, value, as defined by A. R. Smith in 1978) to RGB: the inverse of RGBToHSV for RGB in [0,1]. " \
    "H is wrapped into the hue circle.\n" \
    kHueNote " V is in the units of RGB, S in [0,1].\n" \
    kAsGivenToRGBNote
#define kPluginHSVToRGBIdentifier "net.sf.openfx.HSVToRGB"

#define kPluginRGBToHSLName "RGBToHSL"
#define kPluginRGBToHSLDescription "Convert from RGB to the HSL color model (hue, saturation, lightness, as defined by Joblove and Greenberg in 1978).\n" \
    kHueNote " L = (max(R,G,B) + min(R,G,B)) / 2, in the units of RGB. S is in [0,1]: it is computed from the largest and smallest components clamped to [0,1].\n" \
    kAsGivenRGBToNote
#define kPluginRGBToHSLIdentifier "net.sf.openfx.RGBToHSL"

#define kPluginHSLToRGBName "HSLToRGB"
#define kPluginHSLToRGBDescription "Convert from the HSL color model (hue, saturation, lightness, as defined by Joblove and Greenberg in 1978) to RGB: the inverse of RGBToHSL for RGB in [0,1]. " \
    "H is wrapped into the hue circle.\n" \
    kHueNote " L is in the units of RGB, S in [0,1].\n" \
    kAsGivenToRGBNote
#define kPluginHSLToRGBIdentifier "net.sf.openfx.HSLToRGB"

#define kHSIAbout \
    "The HSI colour space (hue, saturation and intensity) attempts to produce a more intuitive representation of colour. The I axis represents the luminance information. The H and S axes are polar coordinates on the plane orthogonal to I. H is the angle, specified such that red is at zero, green at one third of the hue circle, and blue at two thirds. Hue thus represents what humans implicitly understand as colour. S is the magnitude of the colour vector projected in the plane orthogonal to I, and so represents the difference between pastel colours (low saturation) and vibrant colours (high saturation). The main drawback of this colour space is that hue is undefined if saturation is zero, making error propagation in transformations from the RGB colour space more complicated.\n" \
    "It should also be noted that, although the HSI colour space may be more intuitive, is not \"perceptual\", in the sense that small displacements of equal size in different parts of the colour space will be perceived by human observers as changes of different magnitude. Attempts have been made to define such colour spaces: CIE-LAB and CIE-LUV are two examples."

#define kPluginRGBToHSIName "RGBToHSI"
#define kPluginRGBToHSIDescription "Convert from RGB to the HSI color model (hue, saturation, intensity, as defined by Gonzalez and Woods in 1992).\n" \
    kHueNote " I = (R+G+B)/3, in the units of RGB. S = 1 - min(R,G,B)/I, in [0,1] for non-negative RGB (0 if I is 0).\n" \
    "The conversion uses the RGB values as they are given: no transfer function (gamma) is applied.\n" \
    kHSIAbout
#define kPluginRGBToHSIIdentifier "net.sf.openfx.RGBToHSI"

#define kPluginHSIToRGBName "HSIToRGB"
#define kPluginHSIToRGBDescription "Convert from the HSI color model (hue, saturation, intensity, as defined by Gonzalez and Woods in 1992) to RGB: the inverse of RGBToHSI for RGB in [0,1]. " \
    "H is wrapped into the hue circle.\n" \
    kHueNote " I is in the units of RGB, S in [0,1].\n" \
    kAsGivenToRGBNote "\n" \
    kHSIAbout
#define kPluginHSIToRGBIdentifier "net.sf.openfx.HSIToRGB"

// The transfer functions of the Y'CbCr, Y'PbPr and Y'UV conversions (SupportExt's ofxsLut.h):
// the 601 ones use sRGB, not the BT.601 OETF (which is the BT.709 one).
#define kEncodeSRGB "Linear RGB is first encoded to R'G'B' with the sRGB transfer function (not the BT.601 one, which is the Rec.709 one); negative values become 0."
#define kDecodeSRGB "R'G'B' is then decoded to linear RGB with the sRGB transfer function; negative values become 0."
#define kEncodeRec709 "Linear RGB is first encoded to R'G'B' with the Rec.709 transfer function (OETF, with the more precise constants of Rec.2020); negative values become 0."
#define kDecodeRec709 "R'G'B' is then decoded to linear RGB with the inverse of the Rec.709 OETF; negative values become 0."
#define kYCbCrRange "Y'CbCr is in the video (studio) range, as 8-bit code values divided by 255: Y' goes from 16/255 (black) to about 235/255 (white), and Cb and Cr from 16/255 to 240/255, with 128/255 for neutral colours."
#define kYPbPrRange "Y' goes from 0 (black) to 1 (white), Pb and Pr from -0.5 to 0.5, with 0 for neutral colours."
#define kYUVRange "Y' goes from 0 (black) to 1 (white), U from -0.436 to 0.436 and V from -0.615 to 0.615, with 0 for neutral colours."
#define kMatrix601 "The matrix is the ITU-R BT.601 one (Kr = 0.299, Kb = 0.114)."
#define kMatrix709 "The matrix is the ITU-R BT.709 one (Kr ≈ 0.2126, Kb ≈ 0.0722)."

#define kPluginRGBToYCbCr601Name "RGBToYCbCr601"
#define kPluginRGBToYCbCr601Description "Convert from linear RGB to the Y'CbCr color model (ITU-R BT.601).\n" \
    kEncodeSRGB " " kMatrix601 " " kYCbCrRange
#define kPluginRGBToYCbCr601Identifier "net.sf.openfx.RGBToYCbCr601"

#define kPluginYCbCrToRGB601Name "YCbCrToRGB601"
#define kPluginYCbCrToRGB601Description "Convert from the Y'CbCr color model (ITU-R BT.601) to linear RGB: the inverse of RGBToYCbCr601 for RGB in [0,1].\n" \
    kYCbCrRange " " kMatrix601 " " kDecodeSRGB
#define kPluginYCbCrToRGB601Identifier "net.sf.openfx.YCbCrToRGB601"

#define kPluginRGBToYCbCr709Name "RGBToYCbCr709"
#define kPluginRGBToYCbCr709Description "Convert from linear RGB to the Y'CbCr color model (ITU-R BT.709).\n" \
    kEncodeRec709 " " kMatrix709 " " kYCbCrRange
#define kPluginRGBToYCbCr709Identifier "net.sf.openfx.RGBToYCbCr709"

#define kPluginYCbCrToRGB709Name "YCbCrToRGB709"
#define kPluginYCbCrToRGB709Description "Convert from the Y'CbCr color model (ITU-R BT.709) to linear RGB: the inverse of RGBToYCbCr709 for RGB in [0,1].\n" \
    kYCbCrRange " " kMatrix709 " " kDecodeRec709
#define kPluginYCbCrToRGB709Identifier "net.sf.openfx.YCbCrToRGB709"

#define kPluginRGBToYPbPr601Name "RGBToYPbPr601"
#define kPluginRGBToYPbPr601Description "Convert from linear RGB to the Y'PbPr color model (ITU-R BT.601).\n" \
    kEncodeSRGB " " kMatrix601 " " kYPbPrRange
#define kPluginRGBToYPbPr601Identifier "net.sf.openfx.RGBToYPbPr601"

#define kPluginYPbPrToRGB601Name "YPbPrToRGB601"
#define kPluginYPbPrToRGB601Description "Convert from the Y'PbPr color model (ITU-R BT.601) to linear RGB: the inverse of RGBToYPbPr601 for RGB in [0,1].\n" \
    kYPbPrRange " " kMatrix601 " " kDecodeSRGB
#define kPluginYPbPrToRGB601Identifier "net.sf.openfx.YPbPrToRGB601"

#define kPluginRGBToYPbPr709Name "RGBToYPbPr709"
#define kPluginRGBToYPbPr709Description "Convert from linear RGB to the Y'PbPr color model (ITU-R BT.709).\n" \
    kEncodeRec709 " " kMatrix709 " " kYPbPrRange
#define kPluginRGBToYPbPr709Identifier "net.sf.openfx.RGBToYPbPr709"

#define kPluginYPbPrToRGB709Name "YPbPrToRGB709"
#define kPluginYPbPrToRGB709Description "Convert from the Y'PbPr color model (ITU-R BT.709) to linear RGB: the inverse of RGBToYPbPr709 for RGB in [0,1].\n" \
    kYPbPrRange " " kMatrix709 " " kDecodeRec709
#define kPluginYPbPrToRGB709Identifier "net.sf.openfx.YPbPrToRGB709"

#define kPluginRGBToYUV601Name "RGBToYUV601"
#define kPluginRGBToYUV601Description "Convert from linear RGB to the Y'UV color model (ITU-R BT.601).\n" \
    kEncodeSRGB " " kMatrix601 " " kYUVRange
#define kPluginRGBToYUV601Identifier "net.sf.openfx.RGBToYUV601"

#define kPluginYUVToRGB601Name "YUVToRGB601"
#define kPluginYUVToRGB601Description "Convert from the Y'UV color model (ITU-R BT.601) to linear RGB: the inverse of RGBToYUV601 for RGB in [0,1].\n" \
    kYUVRange " " kMatrix601 " " kDecodeSRGB
#define kPluginYUVToRGB601Identifier "net.sf.openfx.YUVToRGB601"

#define kPluginRGBToYUV709Name "RGBToYUV709"
#define kPluginRGBToYUV709Description "Convert from linear RGB to the Y'UV color model (ITU-R BT.709).\n" \
    kEncodeRec709 " " kMatrix709 " " kYUVRange
#define kPluginRGBToYUV709Identifier "net.sf.openfx.RGBToYUV709"

#define kPluginYUVToRGB709Name "YUVToRGB709"
#define kPluginYUVToRGB709Description "Convert from the Y'UV color model (ITU-R BT.709) to linear RGB: the inverse of RGBToYUV709 for RGB in [0,1].\n" \
    kYUVRange " " kMatrix709 " " kDecodeRec709
#define kPluginYUVToRGB709Identifier "net.sf.openfx.YUVToRGB709"

#define kPluginRGBToYIQName "RGBToYIQ"
#define kPluginRGBToYIQDescription "Convert from RGB to YIQ color model (NTSC 1953, FCC; assuming sRGB/Rec.709 primaries). RGB is gamma-compressed using the sRGB Opto-Electronic Transfer Function (OETF) before conversion. I and Q may be negative, so only floating-point images are supported."
#define kPluginRGBToYIQIdentifier "net.sf.openfx.RGBToYIQ"

#define kPluginYIQToRGBName "YIQToRGB"
#define kPluginYIQToRGBDescription "Convert from YIQ color model (NTSC 1953, FCC; assuming sRGB/Rec.709 primaries) to RGB. RGB is gamma-decompressed using the sRGB Electro-Optical Transfer Function (EOTF) after conversion. I and Q may be negative, so only floating-point images are supported."
#define kPluginYIQToRGBIdentifier "net.sf.openfx.YIQToRGB"

#define kPluginRGBToYDbDrName "RGBToYDbDr"
#define kPluginRGBToYDbDrDescription "Convert from RGB to YDbDr color model (SECAM; assuming sRGB/Rec.709 primaries). RGB is gamma-compressed using the sRGB Opto-Electronic Transfer Function (OETF) before conversion. Db and Dr may be negative, so only floating-point images are supported."
#define kPluginRGBToYDbDrIdentifier "net.sf.openfx.RGBToYDbDr"

#define kPluginYDbDrToRGBName "YDbDrToRGB"
#define kPluginYDbDrToRGBDescription "Convert from YDbDr color model (SECAM; assuming sRGB/Rec.709 primaries) to RGB. RGB is gamma-decompressed using the sRGB Electro-Optical Transfer Function (EOTF) after conversion. Db and Dr may be negative, so only floating-point images are supported."
#define kPluginYDbDrToRGBIdentifier "net.sf.openfx.YDbDrToRGB"


#define kPluginRGB709ToXYZName "RGB709ToXYZ"
#define kPluginRGB709ToXYZDescription "Convert from RGB (Rec.709 with D65 illuminant) to XYZ color model. X, Y and Z are in the same units as RGB."
#define kPluginRGB709ToXYZIdentifier "net.sf.openfx.RGB709ToXYZ"

#define kPluginXYZToRGB709Name "XYZToRGB709"
#define kPluginXYZToRGB709Description "Convert from XYZ color model to RGB (Rec.709 with D65 illuminant). X, Y and Z are in the same units as RGB."
#define kPluginXYZToRGB709Identifier "net.sf.openfx.XYZToRGB709"

#define kPluginRGB709ToLabName "RGB709ToLab"
#define kPluginRGB709ToLabDescription "Convert from RGB (Rec.709 with D65 illuminant) to the CIE L*a*b* color model. The L*a*b* coordinates are divided by 100 for better visualization."
#define kPluginRGB709ToLabIdentifier "net.sf.openfx.RGB709ToLab"

#define kPluginLabToRGB709Name "LabToRGB709"
#define kPluginLabToRGB709Description "Convert from the CIE L*a*b* color model to RGB (Rec.709 with D65 illuminant). The L*a*b* coordinates are divided by 100 for better visualization."
#define kPluginLabToRGB709Identifier "net.sf.openfx.LabToRGB709"

#define kPluginXYZToLabName "XYZToLab"
#define kPluginXYZToLabDescription "Convert from CIE XYZ color space to CIE L*a*b* color space. The L*a*b* coordinates are divided by 100 for better visualization."
#define kPluginXYZToLabIdentifier "net.sf.openfx.XYZToLab"

#define kPluginLabToXYZName "LabToXYZ"
#define kPluginLabToXYZDescription "Convert from CIE L*a*b* color space to CIE XYZ color space. The L*a*b* coordinates are divided by 100 for better visualization."
#define kPluginLabToXYZIdentifier "net.sf.openfx.LabToXYZ"

#define kPluginXYZToxyYName "XYZToxyY"
#define kPluginXYZToxyYDescription "Convert from CIE XYZ color space to CIE xyY color space."
#define kPluginXYZToxyYIdentifier "net.sf.openfx.XYZToxyY"

#define kPluginxyYToXYZName "xyYToXYZ"
#define kPluginxyYToXYZDescription "Convert from CIE xyY color space to CIE XYZ color space."
#define kPluginxyYToXYZIdentifier "net.sf.openfx.xyYToXYZ"

#define kPluginGrouping "Color/Transform"

// history:
// 1.0 initial version
//     (later, plugins were named more consistently and a few conversions were added, without changing the version)
// 1.1 HSVToRGB, HSLToRGB, HSIToRGB wrap the hue into the hue circle (bug fix: HSV and HSL gave
//     magenta for hues in [-1, -5/6) mod 1, and HSI was not periodic for out-of-range hues);
//     all conversions released as 1.0, including version 1 of the signedOutput() conversions
//     (kept for backward compatibility): premultiplying an infinite or NaN colour by alpha 0 gives 0, not NaN (SupportExt #73); an unpremultiplied colour beyond the float range gives +-FLT_MAX, not +-inf (SupportExt #79)
// 1.0 RGBToYIQ, YIQToRGB, RGBToYDbDr, YDbDrToRGB: initial version
// 2.0 RGBToYPbPr601, RGBToYPbPr709, RGBToYUV601, RGBToYUV709, RGB709ToLab, XYZToLab only:
//     only support float images, because their output may be negative (version 1 clamps negative
//     values to 0 on 8-bit and 16-bit images); premultiplying an infinite or NaN colour by alpha 0 gives 0, not NaN (SupportExt #73); an unpremultiplied colour beyond the float range gives +-FLT_MAX, not +-inf (SupportExt #79).
//     The other conversions are still version 1.
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 1 // Increment this when you have fixed a bug or made it faster.
#define kPluginNewVersionMinor 0 // minor version of the conversions added after version 1.0 was released (YIQ, YDbDr)
#define kPluginSignedOutputVersionMajor 2 // version of the conversions with a signed output (see signedOutput())
#define kPluginSignedOutputVersionMinor 0

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamPremultRGBToXXXLabel "Unpremult"
#define kParamPremultRGBToXXXHint \
    "Divide the image by the alpha channel before processing. " \
    "Use if the input images are premultiplied."

#define kParamPremultXXXToRGBLabel "Premult"
#define kParamPremultXXXToRGBHint \
    "Multiply the image by the alpha channel after processing. " \
    "Use to get premultiplied output images."

#define kParamPremultChanged "premultChanged"


enum ColorTransformEnum
{
    eColorTransformRGBToHSV,
    eColorTransformHSVToRGB,
    eColorTransformRGBToHSL,
    eColorTransformHSLToRGB,
    eColorTransformRGBToHSI,
    eColorTransformHSIToRGB,
    eColorTransformRGBToYCbCr601,
    eColorTransformYCbCrToRGB601,
    eColorTransformRGBToYCbCr709,
    eColorTransformYCbCrToRGB709,
    eColorTransformRGBToYPbPr601,
    eColorTransformYPbPrToRGB601,
    eColorTransformRGBToYPbPr709,
    eColorTransformYPbPrToRGB709,
    eColorTransformRGBToYUV601,
    eColorTransformYUVToRGB601,
    eColorTransformRGBToYUV709,
    eColorTransformYUVToRGB709,
    eColorTransformRGBToYIQ,
    eColorTransformYIQToRGB,
    eColorTransformRGBToYDbDr,
    eColorTransformYDbDrToRGB,
    eColorTransformRGB709ToXYZ,
    eColorTransformXYZToRGB709,
    eColorTransformRGB709ToLab,
    eColorTransformLabToRGB709,
    eColorTransformXYZToLab,
    eColorTransformLabToXYZ,
    eColorTransformXYZToxyY,
    eColorTransformxyYToXYZ,
};

#define toRGB(e)   ( (e) == eColorTransformHSVToRGB || \
                     (e) == eColorTransformHSLToRGB || \
                     (e) == eColorTransformHSIToRGB || \
                     (e) == eColorTransformYCbCrToRGB601 || \
                     (e) == eColorTransformYCbCrToRGB709 || \
                     (e) == eColorTransformYPbPrToRGB601 || \
                     (e) == eColorTransformYPbPrToRGB709 || \
                     (e) == eColorTransformYUVToRGB601 || \
                     (e) == eColorTransformYUVToRGB709 || \
                     (e) == eColorTransformYIQToRGB || \
                     (e) == eColorTransformYDbDrToRGB || \
                     (e) == eColorTransformXYZToRGB709 || \
                     (e) == eColorTransformLabToRGB709 )

#define fromRGB(e) ( !toRGB(e) && ( (e) != eColorTransformXYZToLab ) && ( (e) != eColorTransformLabToXYZ ) && ( (e) != eColorTransformXYZToxyY ) && ( (e) != eColorTransformxyYToXYZ ) )

// conversions whose output components are signed, even for an input in the [0,1] range
// (Pb, Pr, U, V, a* and b*), which is not representable in 8-bit or 16-bit images
#define signedOutput(e) ( (e) == eColorTransformRGBToYPbPr601 || \
                          (e) == eColorTransformRGBToYPbPr709 || \
                          (e) == eColorTransformRGBToYUV601 || \
                          (e) == eColorTransformRGBToYUV709 || \
                          (e) == eColorTransformRGB709ToLab || \
                          (e) == eColorTransformXYZToLab )

// conversions with signed components (I, Q, Db, Dr) on input or output, which were
// added after the signedOutput() conversions became float-only: they only have a
// version 1, and it only supports float images
#define floatOnlyAllVersions(e) ( (e) == eColorTransformRGBToYIQ || \
                             (e) == eColorTransformYIQToRGB || \
                             (e) == eColorTransformRGBToYDbDr || \
                             (e) == eColorTransformYDbDrToRGB )

// YIQ (NTSC 1953, FCC), applied to gamma-compressed R'G'B'.
// The precise form is used: U = 0.492111(B'-Y') and V = 0.877283(R'-Y') rotated by 33 degrees
// (the rounded 1953 formula I = 0.74(R'-Y') - 0.27(B'-Y'), Q = 0.48(R'-Y') + 0.41(B'-Y') differs by < 0.005).
// Forward coefficients from https://en.wikipedia.org/wiki/YIQ (FCC NTSC standard);
// the inverse is the exact (double-precision) inverse of the forward matrix.
static inline void
rgb_to_yiq(float r, float g, float b, float *y, float *i, float *q)
{
    *y = 0.299f    * r + 0.587f    * g + 0.114f    * b;
    *i = 0.595716f * r - 0.274453f * g - 0.321263f * b;
    *q = 0.211456f * r - 0.522591f * g + 0.311135f * b;
}

static inline void
yiq_to_rgb(float y, float i, float q, float *r, float *g, float *b)
{
    *r = y + 0.9562957198f * i + 0.6210244165f * q;
    *g = y - 0.2721220993f * i - 0.6473805968f * q;
    *b = y - 1.1069890167f * i + 1.7046149984f * q;
}

// YDbDr (SECAM), applied to gamma-compressed R'G'B'.
// Forward coefficients from https://en.wikipedia.org/wiki/YDbDr (SECAM standard);
// the inverse is the exact (double-precision) inverse of the forward matrix.
static inline void
rgb_to_ydbdr(float r, float g, float b, float *y, float *db, float *dr)
{
    *y  =  0.299f * r + 0.587f * g + 0.114f * b;
    *db = -0.450f * r - 0.883f * g + 1.333f * b;
    *dr = -1.333f * r + 1.116f * g + 0.217f * b;
}

static inline void
ydbdr_to_rgb(float y, float db, float dr, float *r, float *g, float *b)
{
    *r = y + 0.0000923037f * db - 0.5259126307f * dr;
    *g = y - 0.1291328989f * db + 0.2678993282f * dr;
    *b = y + 0.6646790600f * db - 0.0000792025f * dr;
}

class ColorTransformProcessorBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg;
    bool _premult;
    int _premultChannel;
    double _mix;

public:

    ColorTransformProcessorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _srcImg(NULL)
        , _premult(false)
        , _premultChannel(3)
        , _mix(1.)
    {
    }

    void setSrcImg(const Image *v) {_srcImg = v; }

    void setValues(bool premult,
                   int premultChannel)
    {
        _premult = premult;
        _premultChannel = premultChannel;
    }

private:
};


template <class PIX, int nComponents, int maxValue, ColorTransformEnum transform>
class ColorTransformProcessor
    : public ColorTransformProcessorBase
{
public:
    ColorTransformProcessor(ImageEffect &instance)
        : ColorTransformProcessorBase(instance)
    {
    }

    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        unused(rs);
        assert(nComponents == 3 || nComponents == 4);
        assert(_dstImg);
        float unpPix[4] = {0.f, 0.f, 0.f, 0.f};
        float tmpPix[4] = {0.f, 0.f, 0.f, 0.f};
        const bool dounpremult = _premult && fromRGB(transform);
        const bool dopremult = _premult && toRGB(transform);

        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                ofxsUnPremult<PIX, nComponents, maxValue>(srcPix, unpPix, dounpremult, _premultChannel);
                switch (transform) {
                case eColorTransformRGBToHSV:
                    // HSV, HSL and HSI are computed on the values as given, with no transfer
                    // function: as Alvy Ray Smith said in his paper,
                    // "We shall assume that an RGB monitor is a linear device"
                    // (see https://github.com/NatronGitHub/Natron/issues/286).
                    // The descriptions tell users to convert to display-referred values first.
                    // Reference:
                    // "Color gamut transform pairs", Alvy Ray Smith, Proceeding SIGGRAPH '78
                    // https://doi.org/10.1145/800248.807361
                    // http://www.icst.pku.edu.cn/F/course/ImageProcessing/2018/resource/Color78.pdf
                    Color::rgb_to_hsv(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformHSVToRGB:
                    Color::hsv_to_rgb(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2], true);
                    // values as given, see the comment above
                    break;

                case eColorTransformRGBToHSL:
                    // values as given, see the comment above
                    Color::rgb_to_hsl(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformHSLToRGB:
                    Color::hsl_to_rgb(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2], true);
                    // values as given, see the comment above
                    break;

                case eColorTransformRGBToHSI:
                    // values as given, see the comment above
                    Color::rgb_to_hsi(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformHSIToRGB:
                    Color::hsi_to_rgb(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2], true);
                    // values as given, see the comment above
                    break;


                case eColorTransformRGBToYCbCr601:
                    unpPix[0] = Color::to_func_srgb(unpPix[0]);
                    unpPix[1] = Color::to_func_srgb(unpPix[1]);
                    unpPix[2] = Color::to_func_srgb(unpPix[2]);
                    Color::rgb_to_ycbcr601(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformYCbCrToRGB601:
                    Color::ycbcr_to_rgb601(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    tmpPix[0] = Color::from_func_srgb(tmpPix[0]);
                    tmpPix[1] = Color::from_func_srgb(tmpPix[1]);
                    tmpPix[2] = Color::from_func_srgb(tmpPix[2]);
                    break;

                case eColorTransformRGBToYCbCr709:
                    unpPix[0] = Color::to_func_Rec709(unpPix[0]);
                    unpPix[1] = Color::to_func_Rec709(unpPix[1]);
                    unpPix[2] = Color::to_func_Rec709(unpPix[2]);
                    Color::rgb_to_ycbcr709(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformYCbCrToRGB709:
                    Color::ycbcr_to_rgb709(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    tmpPix[0] = Color::from_func_Rec709(tmpPix[0]);
                    tmpPix[1] = Color::from_func_Rec709(tmpPix[1]);
                    tmpPix[2] = Color::from_func_Rec709(tmpPix[2]);
                    break;

                case eColorTransformRGBToYPbPr601:
                    unpPix[0] = Color::to_func_srgb(unpPix[0]);
                    unpPix[1] = Color::to_func_srgb(unpPix[1]);
                    unpPix[2] = Color::to_func_srgb(unpPix[2]);
                    Color::rgb_to_ypbpr601(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformYPbPrToRGB601:
                    Color::ypbpr_to_rgb601(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    tmpPix[0] = Color::from_func_srgb(tmpPix[0]);
                    tmpPix[1] = Color::from_func_srgb(tmpPix[1]);
                    tmpPix[2] = Color::from_func_srgb(tmpPix[2]);
                    break;

                case eColorTransformRGBToYPbPr709:
                    unpPix[0] = Color::to_func_Rec709(unpPix[0]);
                    unpPix[1] = Color::to_func_Rec709(unpPix[1]);
                    unpPix[2] = Color::to_func_Rec709(unpPix[2]);
                    Color::rgb_to_ypbpr709(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformYPbPrToRGB709:
                    Color::ypbpr_to_rgb709(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    tmpPix[0] = Color::from_func_Rec709(tmpPix[0]);
                    tmpPix[1] = Color::from_func_Rec709(tmpPix[1]);
                    tmpPix[2] = Color::from_func_Rec709(tmpPix[2]);
                    break;

                case eColorTransformRGBToYUV601:
                    unpPix[0] = Color::to_func_srgb(unpPix[0]);
                    unpPix[1] = Color::to_func_srgb(unpPix[1]);
                    unpPix[2] = Color::to_func_srgb(unpPix[2]);
                    Color::rgb_to_yuv601(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformYUVToRGB601:
                    Color::yuv_to_rgb601(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    tmpPix[0] = Color::from_func_srgb(tmpPix[0]);
                    tmpPix[1] = Color::from_func_srgb(tmpPix[1]);
                    tmpPix[2] = Color::from_func_srgb(tmpPix[2]);
                    break;

                case eColorTransformRGBToYUV709:
                    unpPix[0] = Color::to_func_Rec709(unpPix[0]);
                    unpPix[1] = Color::to_func_Rec709(unpPix[1]);
                    unpPix[2] = Color::to_func_Rec709(unpPix[2]);
                    Color::rgb_to_yuv709(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformYUVToRGB709:
                    Color::yuv_to_rgb709(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    tmpPix[0] = Color::from_func_Rec709(tmpPix[0]);
                    tmpPix[1] = Color::from_func_Rec709(tmpPix[1]);
                    tmpPix[2] = Color::from_func_Rec709(tmpPix[2]);
                    break;

                case eColorTransformRGBToYIQ:
                    unpPix[0] = Color::to_func_srgb(unpPix[0]);
                    unpPix[1] = Color::to_func_srgb(unpPix[1]);
                    unpPix[2] = Color::to_func_srgb(unpPix[2]);
                    rgb_to_yiq(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformYIQToRGB:
                    yiq_to_rgb(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    tmpPix[0] = Color::from_func_srgb(tmpPix[0]);
                    tmpPix[1] = Color::from_func_srgb(tmpPix[1]);
                    tmpPix[2] = Color::from_func_srgb(tmpPix[2]);
                    break;

                case eColorTransformRGBToYDbDr:
                    unpPix[0] = Color::to_func_srgb(unpPix[0]);
                    unpPix[1] = Color::to_func_srgb(unpPix[1]);
                    unpPix[2] = Color::to_func_srgb(unpPix[2]);
                    rgb_to_ydbdr(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformYDbDrToRGB:
                    ydbdr_to_rgb(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    tmpPix[0] = Color::from_func_srgb(tmpPix[0]);
                    tmpPix[1] = Color::from_func_srgb(tmpPix[1]);
                    tmpPix[2] = Color::from_func_srgb(tmpPix[2]);
                    break;

                case eColorTransformRGB709ToXYZ:
                    Color::rgb709_to_xyz(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformXYZToRGB709:
                    Color::xyz_to_rgb709(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformRGB709ToLab:
                    Color::rgb709_to_lab(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    tmpPix[0] /= 100;
                    tmpPix[1] /= 100;
                    tmpPix[2] /= 100;
                    break;

                case eColorTransformLabToRGB709:
                    unpPix[0] *= 100;
                    unpPix[1] *= 100;
                    unpPix[2] *= 100;
                    Color::lab_to_rgb709(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformXYZToLab:
                    Color::xyz_to_lab(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    tmpPix[0] /= 100;
                    tmpPix[1] /= 100;
                    tmpPix[2] /= 100;
                    break;

                case eColorTransformLabToXYZ:
                    unpPix[0] *= 100;
                    unpPix[1] *= 100;
                    unpPix[2] *= 100;
                    Color::lab_to_xyz(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformXYZToxyY: {
                    float X = unpPix[0];
                    float Y = unpPix[1];
                    float Z = unpPix[2];
                    float XYZ = X + Y + Z;
                    float invXYZ = XYZ <= 0 ? 0. : (1. / XYZ);
                    tmpPix[0] = X * invXYZ;
                    tmpPix[1] = Y * invXYZ;
                    tmpPix[2] = Y;
                    break;
                }
                case eColorTransformxyYToXYZ: {
                    float x = unpPix[0];
                    float y = unpPix[1];
                    float Y = unpPix[2];
                    float invy = (y <= 0) ? 0. : (1 / y);
                    tmpPix[0] = x * Y * invy;
                    tmpPix[1] = Y;
                    tmpPix[2] = (1 - x - y) * Y * invy;
                    break;
                }
                } // switch
                tmpPix[3] = unpPix[3];
                ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, dopremult, _premultChannel, x, y, srcPix, /*doMasking=*/ false, /*maskImg=*/ NULL, /*mix=*/ 1.f, /*maskInvert=*/ false, dstPix);
                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    } // multiThreadProcessImages
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
template <ColorTransformEnum transform>
class ColorTransformPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    ColorTransformPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _premult(NULL)
        , _premultChannel(NULL)
        , _premultChanged(NULL)
    {

        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA) ) );
        if ( fromRGB(transform) || toRGB(transform) ) {
            _premult = fetchBooleanParam(kParamPremult);
            _premultChannel = fetchChoiceParam(kParamPremultChannel);
            assert(_premult && _premultChannel);
            _premultChanged = fetchBooleanParam(kParamPremultChanged);
            assert(_premultChanged);
        }
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(ColorTransformProcessorBase &, const RenderArguments &args);

    /** @brief get the clip preferences */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    BooleanParam* _premult;
    ChoiceParam* _premultChannel;
    BooleanParam* _premultChanged; // set to true the first time the user connects src
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
template <ColorTransformEnum transform>
void
ColorTransformPlugin<transform>::setupAndProcess(ColorTransformProcessorBase &processor,
                                                 const RenderArguments &args)
{
    auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );

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
                                    _srcClip->fetchImage(args.time) : 0 );
    if ( src.get() ) {
# ifndef NDEBUG
        checkBadRenderScaleOrField(src, args);
# endif
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );
    processor.setRenderWindow(args.renderWindow, args.renderScale);

    bool premult = false;
    if (_premult) {
        _premult->getValueAtTime(args.time, premult);
    }
    int premultChannel = 3;
    if (_premultChannel) {
        _premultChannel->getValueAtTime(args.time, premultChannel);
    }

    processor.setValues(premult, premultChannel);
    processor.process();
} // >::setupAndProcess

// the overridden render function
template <ColorTransformEnum transform>
void
ColorTransformPlugin<transform>::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(dstComponents == ePixelComponentRGB || dstComponents == ePixelComponentRGBA);
    if (dstComponents == ePixelComponentRGBA) {
        switch (dstBitDepth) {
        case eBitDepthUByte: {
            ColorTransformProcessor<unsigned char, 4, 255, transform> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthUShort: {
            ColorTransformProcessor<unsigned short, 4, 65535, transform> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthFloat: {
            ColorTransformProcessor<float, 4, 1, transform> fred(*this);
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
            ColorTransformProcessor<unsigned char, 3, 255, transform> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthUShort: {
            ColorTransformProcessor<unsigned short, 3, 65535, transform> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthFloat: {
            ColorTransformProcessor<float, 3, 1, transform> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        default:
            throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
} // >::render

/* Override the clip preferences */
template <ColorTransformEnum transform>
void
ColorTransformPlugin<transform>::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    if ( ( fromRGB(transform) || toRGB(transform) ) &&
         _srcClip &&
         ( !_srcClip->isConnected() || (_srcClip->getPixelComponents() ==  ePixelComponentRGBA) ) ) {
        bool premult;
        _premult->getValue(premult);
        // set the premultiplication of _dstClip
        if ( fromRGB(transform) ) {
            // HSV is always unpremultiplied
            clipPreferences.setOutputPremultiplication(eImageUnPreMultiplied);
        } else {
            // RGB
            clipPreferences.setOutputPremultiplication(premult ? eImagePreMultiplied : eImageUnPreMultiplied);
        }
    }
}

template <ColorTransformEnum transform>
void
ColorTransformPlugin<transform>::changedClip(const InstanceChangedArgs &args,
                                             const std::string &clipName)
{
    if ( ( fromRGB(transform) || toRGB(transform) ) &&
         (clipName == kOfxImageEffectSimpleSourceClipName) &&
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

template <ColorTransformEnum transform>
void
ColorTransformPlugin<transform>::changedParam(const InstanceChangedArgs &args,
                                              const std::string &paramName)
{
    if ( ( fromRGB(transform) || toRGB(transform) ) && (paramName == kParamPremult) && (args.reason == eChangeUserEdit) ) {
        _premultChanged->setValue(true);
    }
}

//mDeclarePluginFactory(ColorTransformPluginFactory, {ofxsThreadSuiteCheck();}, {});
// each major version needs its own factory class, see mDeclarePluginFactoryVersioned
template <ColorTransformEnum transform, unsigned int majorVersion>
class ColorTransformPluginFactory
    : public PluginFactoryHelper<ColorTransformPluginFactory<transform, majorVersion> >
{
public:
    ColorTransformPluginFactory(const std::string& id,
                                unsigned int verMin) : PluginFactoryHelper<ColorTransformPluginFactory<transform, majorVersion> >(id, majorVersion, verMin) {}

    virtual void load() OVERRIDE FINAL {ofxsThreadSuiteCheck();};
    //virtual void unload() OVERRIDE FINAL {};
    virtual void describe(ImageEffectDescriptor &desc) OVERRIDE FINAL;
    virtual void describeInContext(ImageEffectDescriptor &desc, ContextEnum context) OVERRIDE FINAL;
    virtual ImageEffect* createInstance(OfxImageEffectHandle handle, ContextEnum context) OVERRIDE FINAL;
};

template <ColorTransformEnum transform, unsigned int majorVersion>
void
ColorTransformPluginFactory<transform, majorVersion>::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    switch (transform) {
    case eColorTransformRGBToHSV:
        desc.setLabel(kPluginRGBToHSVName);
        desc.setPluginDescription(kPluginRGBToHSVDescription);
        break;

    case eColorTransformHSVToRGB:
        desc.setLabel(kPluginHSVToRGBName);
        desc.setPluginDescription(kPluginHSVToRGBDescription);
        break;

    case eColorTransformRGBToHSL:
        desc.setLabel(kPluginRGBToHSLName);
        desc.setPluginDescription(kPluginRGBToHSLDescription);
        break;

    case eColorTransformHSLToRGB:
        desc.setLabel(kPluginHSLToRGBName);
        desc.setPluginDescription(kPluginHSLToRGBDescription);
        break;

    case eColorTransformRGBToHSI:
        desc.setLabel(kPluginRGBToHSIName);
        desc.setPluginDescription(kPluginRGBToHSIDescription);
        break;

    case eColorTransformHSIToRGB:
        desc.setLabel(kPluginHSIToRGBName);
        desc.setPluginDescription(kPluginHSIToRGBDescription);
        break;

    case eColorTransformRGBToYCbCr601:
        desc.setLabel(kPluginRGBToYCbCr601Name);
        desc.setPluginDescription(kPluginRGBToYCbCr601Description);
        break;

    case eColorTransformYCbCrToRGB601:
        desc.setLabel(kPluginYCbCrToRGB601Name);
        desc.setPluginDescription(kPluginYCbCrToRGB601Description);
        break;

    case eColorTransformRGBToYCbCr709:
        desc.setLabel(kPluginRGBToYCbCr709Name);
        desc.setPluginDescription(kPluginRGBToYCbCr709Description);
        break;

    case eColorTransformYCbCrToRGB709:
        desc.setLabel(kPluginYCbCrToRGB709Name);
        desc.setPluginDescription(kPluginYCbCrToRGB709Description);
        break;

    case eColorTransformRGBToYPbPr601:
        desc.setLabel(kPluginRGBToYPbPr601Name);
        desc.setPluginDescription(kPluginRGBToYPbPr601Description);
        break;

    case eColorTransformYPbPrToRGB601:
        desc.setLabel(kPluginYPbPrToRGB601Name);
        desc.setPluginDescription(kPluginYPbPrToRGB601Description);
        break;

    case eColorTransformRGBToYPbPr709:
        desc.setLabel(kPluginRGBToYPbPr709Name);
        desc.setPluginDescription(kPluginRGBToYPbPr709Description);
        break;

    case eColorTransformYPbPrToRGB709:
        desc.setLabel(kPluginYPbPrToRGB709Name);
        desc.setPluginDescription(kPluginYPbPrToRGB709Description);
        break;

    case eColorTransformRGBToYUV601:
        desc.setLabel(kPluginRGBToYUV601Name);
        desc.setPluginDescription(kPluginRGBToYUV601Description);
        break;

    case eColorTransformYUVToRGB601:
        desc.setLabel(kPluginYUVToRGB601Name);
        desc.setPluginDescription(kPluginYUVToRGB601Description);
        break;

    case eColorTransformRGBToYUV709:
        desc.setLabel(kPluginRGBToYUV709Name);
        desc.setPluginDescription(kPluginRGBToYUV709Description);
        break;

    case eColorTransformYUVToRGB709:
        desc.setLabel(kPluginYUVToRGB709Name);
        desc.setPluginDescription(kPluginYUVToRGB709Description);
        break;

    case eColorTransformRGBToYIQ:
        desc.setLabel(kPluginRGBToYIQName);
        desc.setPluginDescription(kPluginRGBToYIQDescription);
        break;

    case eColorTransformYIQToRGB:
        desc.setLabel(kPluginYIQToRGBName);
        desc.setPluginDescription(kPluginYIQToRGBDescription);
        break;

    case eColorTransformRGBToYDbDr:
        desc.setLabel(kPluginRGBToYDbDrName);
        desc.setPluginDescription(kPluginRGBToYDbDrDescription);
        break;

    case eColorTransformYDbDrToRGB:
        desc.setLabel(kPluginYDbDrToRGBName);
        desc.setPluginDescription(kPluginYDbDrToRGBDescription);
        break;

    case eColorTransformRGB709ToXYZ:
        desc.setLabel(kPluginRGB709ToXYZName);
        desc.setPluginDescription(kPluginRGB709ToXYZDescription);
        break;

    case eColorTransformXYZToRGB709:
        desc.setLabel(kPluginXYZToRGB709Name);
        desc.setPluginDescription(kPluginXYZToRGB709Description);
        break;

    case eColorTransformRGB709ToLab:
        desc.setLabel(kPluginRGB709ToLabName);
        desc.setPluginDescription(kPluginRGB709ToLabDescription);
        break;

    case eColorTransformLabToRGB709:
        desc.setLabel(kPluginLabToRGB709Name);
        desc.setPluginDescription(kPluginLabToRGB709Description);
        break;

    case eColorTransformXYZToLab:
        desc.setLabel(kPluginXYZToLabName);
        desc.setPluginDescription(kPluginXYZToLabDescription);
        break;

    case eColorTransformLabToXYZ:
        desc.setLabel(kPluginLabToXYZName);
        desc.setPluginDescription(kPluginLabToXYZDescription);
        break;

    case eColorTransformXYZToxyY:
        desc.setLabel(kPluginXYZToxyYName);
        desc.setPluginDescription(kPluginXYZToxyYDescription);
        break;

    case eColorTransformxyYToXYZ:
        desc.setLabel(kPluginxyYToXYZName);
        desc.setPluginDescription(kPluginxyYToXYZDescription);
        break;
    } // switch
    desc.setPluginGrouping(kPluginGrouping);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    //desc.addSupportedContext(eContextPaint);
    if ( ( signedOutput(transform) && (majorVersion >= kPluginSignedOutputVersionMajor) ) || floatOnlyAllVersions(transform) ) {
        // the output may be negative: only support float images
        desc.addSupportedBitDepth(eBitDepthFloat);
    } else {
        // version 1 of signedOutput() conversions clamps negative values to 0 on 8-bit and 16-bit images
        desc.addSupportedBitDepth(eBitDepthUByte);
        desc.addSupportedBitDepth(eBitDepthUShort);
        desc.addSupportedBitDepth(eBitDepthFloat);
        if ( signedOutput(transform) ) {
            desc.setIsObsolete(true);
        }
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
    desc.setRenderThreadSafety(kRenderThreadSafety);
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentRGB);
#endif
} // >::describe

template <ColorTransformEnum transform, unsigned int majorVersion>
void
ColorTransformPluginFactory<transform, majorVersion>::describeInContext(ImageEffectDescriptor &desc,
                                                          ContextEnum /*context*/)
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

    // make some pages and to things in
    if ( fromRGB(transform) || toRGB(transform) ) {
        PageParamDescriptor *page = desc.definePageParam("Controls");
        {
            BooleanParamDescriptor* param = desc.defineBooleanParam(kParamPremult);
            if ( fromRGB(transform) ) {
                param->setLabel(kParamPremultRGBToXXXLabel);
                param->setHint(kParamPremultRGBToXXXHint);
            } else {
                param->setLabel(kParamPremultXXXToRGBLabel);
                param->setHint(kParamPremultXXXToRGBHint);
            }
            param->setLayoutHint(eLayoutHintNoNewLine, 1);
            param->setAnimates(false);
            desc.addClipPreferencesSlaveParam(*param);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            // not yet implemented, for future use (whenever deep compositing is supported)
            ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamPremultChannel);
            param->setLabel(kParamPremultChannelLabel);
            param->setHint(kParamPremultChannelHint);
            param->appendOption(kParamPremultChannelR);
            param->appendOption(kParamPremultChannelG);
            param->appendOption(kParamPremultChannelB);
            param->appendOption(kParamPremultChannelA);
            param->setDefault(3); // alpha
            param->setIsSecretAndDisabled(true); // not yet implemented
            if (page) {
                page->addChild(*param);
            }
        }

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
    }
} // >::describeInContext

template <ColorTransformEnum transform, unsigned int majorVersion>
ImageEffect*
ColorTransformPluginFactory<transform, majorVersion>::createInstance(OfxImageEffectHandle handle,
                                                       ContextEnum /*context*/)
{
    return new ColorTransformPlugin<transform>(handle);
}

// The signedOutput() conversions also register their version 1 (p*v1, obsolete, supports
// 8-bit and 16-bit images), so that old projects render the same.

// RGBtoHSV
static ColorTransformPluginFactory<eColorTransformRGBToHSV, kPluginVersionMajor> p1(kPluginRGBToHSVIdentifier, kPluginVersionMinor);
// HSVtoRGB
static ColorTransformPluginFactory<eColorTransformHSVToRGB, kPluginVersionMajor> p2(kPluginHSVToRGBIdentifier, kPluginVersionMinor);
// RGBtoHSL
static ColorTransformPluginFactory<eColorTransformRGBToHSL, kPluginVersionMajor> p3(kPluginRGBToHSLIdentifier, kPluginVersionMinor);
// HSLtoRGB
static ColorTransformPluginFactory<eColorTransformHSLToRGB, kPluginVersionMajor> p4(kPluginHSLToRGBIdentifier, kPluginVersionMinor);
// RGBtoHSI
static ColorTransformPluginFactory<eColorTransformRGBToHSI, kPluginVersionMajor> p5(kPluginRGBToHSIIdentifier, kPluginVersionMinor);
// HSItoRGB
static ColorTransformPluginFactory<eColorTransformHSIToRGB, kPluginVersionMajor> p6(kPluginHSIToRGBIdentifier, kPluginVersionMinor);
// RGBToYCbCr601
static ColorTransformPluginFactory<eColorTransformRGBToYCbCr601, kPluginVersionMajor> p7(kPluginRGBToYCbCr601Identifier, kPluginVersionMinor);
// YCbCrToRGB601
static ColorTransformPluginFactory<eColorTransformYCbCrToRGB601, kPluginVersionMajor> p8(kPluginYCbCrToRGB601Identifier, kPluginVersionMinor);
// RGBToYCbCr709
static ColorTransformPluginFactory<eColorTransformRGBToYCbCr709, kPluginVersionMajor> p17(kPluginRGBToYCbCr709Identifier, kPluginVersionMinor);
// YCbCrToRGB709
static ColorTransformPluginFactory<eColorTransformYCbCrToRGB709, kPluginVersionMajor> p18(kPluginYCbCrToRGB709Identifier, kPluginVersionMinor);
// RGBToYPbPr601
static ColorTransformPluginFactory<eColorTransformRGBToYPbPr601, kPluginSignedOutputVersionMajor> p9(kPluginRGBToYPbPr601Identifier, kPluginSignedOutputVersionMinor);
static ColorTransformPluginFactory<eColorTransformRGBToYPbPr601, 1> p9v1(kPluginRGBToYPbPr601Identifier, kPluginVersionMinor);
// YPbPrToRGB601
static ColorTransformPluginFactory<eColorTransformYPbPrToRGB601, kPluginVersionMajor> p10(kPluginYPbPrToRGB601Identifier, kPluginVersionMinor);
// RGBToYPbPr709
static ColorTransformPluginFactory<eColorTransformRGBToYPbPr709, kPluginSignedOutputVersionMajor> p15(kPluginRGBToYPbPr709Identifier, kPluginSignedOutputVersionMinor);
static ColorTransformPluginFactory<eColorTransformRGBToYPbPr709, 1> p15v1(kPluginRGBToYPbPr709Identifier, kPluginVersionMinor);
// YPbPrToRGB709
static ColorTransformPluginFactory<eColorTransformYPbPrToRGB709, kPluginVersionMajor> p16(kPluginYPbPrToRGB709Identifier, kPluginVersionMinor);
// RGBToYUV601
static ColorTransformPluginFactory<eColorTransformRGBToYUV601, kPluginSignedOutputVersionMajor> p19(kPluginRGBToYUV601Identifier, kPluginSignedOutputVersionMinor);
static ColorTransformPluginFactory<eColorTransformRGBToYUV601, 1> p19v1(kPluginRGBToYUV601Identifier, kPluginVersionMinor);
// YUVToRGB601
static ColorTransformPluginFactory<eColorTransformYUVToRGB601, kPluginVersionMajor> p20(kPluginYUVToRGB601Identifier, kPluginVersionMinor);
// RGBToYUV709
static ColorTransformPluginFactory<eColorTransformRGBToYUV709, kPluginSignedOutputVersionMajor> p21(kPluginRGBToYUV709Identifier, kPluginSignedOutputVersionMinor);
static ColorTransformPluginFactory<eColorTransformRGBToYUV709, 1> p21v1(kPluginRGBToYUV709Identifier, kPluginVersionMinor);
// YUVToRGB709
static ColorTransformPluginFactory<eColorTransformYUVToRGB709, kPluginVersionMajor> p22(kPluginYUVToRGB709Identifier, kPluginVersionMinor);
// RGBToYIQ
static ColorTransformPluginFactory<eColorTransformRGBToYIQ, kPluginVersionMajor> p27(kPluginRGBToYIQIdentifier, kPluginNewVersionMinor);
// YIQToRGB
static ColorTransformPluginFactory<eColorTransformYIQToRGB, kPluginVersionMajor> p28(kPluginYIQToRGBIdentifier, kPluginNewVersionMinor);
// RGBToYDbDr
static ColorTransformPluginFactory<eColorTransformRGBToYDbDr, kPluginVersionMajor> p29(kPluginRGBToYDbDrIdentifier, kPluginNewVersionMinor);
// YDbDrToRGB
static ColorTransformPluginFactory<eColorTransformYDbDrToRGB, kPluginVersionMajor> p30(kPluginYDbDrToRGBIdentifier, kPluginNewVersionMinor);
// RGB709ToXYZ
static ColorTransformPluginFactory<eColorTransformRGB709ToXYZ, kPluginVersionMajor> p11(kPluginRGB709ToXYZIdentifier, kPluginVersionMinor);
// XYZToRGB709
static ColorTransformPluginFactory<eColorTransformXYZToRGB709, kPluginVersionMajor> p12(kPluginXYZToRGB709Identifier, kPluginVersionMinor);
// RGB709ToLab
static ColorTransformPluginFactory<eColorTransformRGB709ToLab, kPluginSignedOutputVersionMajor> p13(kPluginRGB709ToLabIdentifier, kPluginSignedOutputVersionMinor);
static ColorTransformPluginFactory<eColorTransformRGB709ToLab, 1> p13v1(kPluginRGB709ToLabIdentifier, kPluginVersionMinor);
// LabToRGB709
static ColorTransformPluginFactory<eColorTransformLabToRGB709, kPluginVersionMajor> p14(kPluginLabToRGB709Identifier, kPluginVersionMinor);
// XYZToLab
static ColorTransformPluginFactory<eColorTransformXYZToLab, kPluginSignedOutputVersionMajor> p23(kPluginXYZToLabIdentifier, kPluginSignedOutputVersionMinor);
static ColorTransformPluginFactory<eColorTransformXYZToLab, 1> p23v1(kPluginXYZToLabIdentifier, kPluginVersionMinor);
// LabToXYZ
static ColorTransformPluginFactory<eColorTransformLabToXYZ, kPluginVersionMajor> p24(kPluginLabToXYZIdentifier, kPluginVersionMinor);
// XYZToxyY
static ColorTransformPluginFactory<eColorTransformXYZToxyY, kPluginVersionMajor> p25(kPluginXYZToxyYIdentifier, kPluginVersionMinor);
// xyYToXYZ
static ColorTransformPluginFactory<eColorTransformxyYToXYZ, kPluginVersionMajor> p26(kPluginxyYToXYZIdentifier, kPluginVersionMinor);

mRegisterPluginFactoryInstance(p1)
mRegisterPluginFactoryInstance(p2)
mRegisterPluginFactoryInstance(p3)
mRegisterPluginFactoryInstance(p4)
mRegisterPluginFactoryInstance(p5)
mRegisterPluginFactoryInstance(p6)
mRegisterPluginFactoryInstance(p7)
mRegisterPluginFactoryInstance(p8)
mRegisterPluginFactoryInstance(p9)
mRegisterPluginFactoryInstance(p9v1)
mRegisterPluginFactoryInstance(p10)
mRegisterPluginFactoryInstance(p11)
mRegisterPluginFactoryInstance(p12)
mRegisterPluginFactoryInstance(p13)
mRegisterPluginFactoryInstance(p13v1)
mRegisterPluginFactoryInstance(p14)
mRegisterPluginFactoryInstance(p15)
mRegisterPluginFactoryInstance(p15v1)
mRegisterPluginFactoryInstance(p16)
mRegisterPluginFactoryInstance(p17)
mRegisterPluginFactoryInstance(p18)
mRegisterPluginFactoryInstance(p19)
mRegisterPluginFactoryInstance(p19v1)
mRegisterPluginFactoryInstance(p20)
mRegisterPluginFactoryInstance(p21)
mRegisterPluginFactoryInstance(p21v1)
mRegisterPluginFactoryInstance(p22)
mRegisterPluginFactoryInstance(p23)
mRegisterPluginFactoryInstance(p23v1)
mRegisterPluginFactoryInstance(p24)
mRegisterPluginFactoryInstance(p25)
mRegisterPluginFactoryInstance(p26)
mRegisterPluginFactoryInstance(p27)
mRegisterPluginFactoryInstance(p28)
mRegisterPluginFactoryInstance(p29)
mRegisterPluginFactoryInstance(p30)

OFXS_NAMESPACE_ANONYMOUS_EXIT
