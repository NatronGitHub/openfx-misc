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
 * OFX WhiteBalance plugin.
 *
 * A chromatic adaptation transform (CAT) from a source white to a target
 * white, in linear RGB with known primaries and white point:
 *
 *   M = RGBtoXYZ^-1 . CAT^-1 . diag(CAT.dst / CAT.src) . CAT . RGBtoXYZ
 *
 * The whites are given by a correlated color temperature (CCT) and a tint
 * (an offset from the locus, perpendicular to it, in the CIE 1960 UCS uv
 * diagram), or the source white is given by a picked neutral color.
 */

#include <cmath>
#include <cfloat> // DBL_MAX
#include <algorithm>

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsRGBAProcessing.h"
#include "ofxsCoords.h"
#include "ofxsMatrix2D.h"
#include "ofxsMacros.h"
#include "ofxsThreadSuite.h"
#ifdef OFX_EXTENSIONS_NATRON
#include "ofxNatron.h"
#endif

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "WhiteBalanceOFX"
#define kPluginGrouping "Color"
#define kPluginDescription \
    "Correct the white balance of an image, by a chromatic adaptation from a source white to a target white.\n" \
    "\n" \
    "The source white is the color of the scene illuminant, i.e. the color that should become the target white " \
    "(as the white balance of a camera or of a raw developer, e.g. the Temp and Tint of Lightroom or Camera Raw). " \
    "The target white is the white the image is adapted to, by default the Working Space White (R=G=B). For example, " \
    "a scene lit by tungsten light is corrected by setting the Source Temperature to about 3200 K (which makes the " \
    "image bluer). With Target White set to Temperature/Tint, a daylight image is warmed up by setting the Target " \
    "Temperature below the Source Temperature: this is the source/target illuminant convention of the Chromatic " \
    "Adaptation tools of e.g. DaVinci Resolve.\n" \
    "The effect is the identity when the source and target whites are given by the same values. The default " \
    "(Source Temperature 6504 K with no tint, i.e. D65, and the Working Space White target) is the identity in the " \
    "D65 working spaces (Rec. 709, Rec. 2020, P3-D65). In the ACES and XYZ working spaces, whose white is not D65, " \
    "it adapts D65 to the working space white.\n" \
    "\n" \
    "Each white is given by:\n" \
    "- a correlated color temperature in Kelvin: a point on the CIE daylight locus (CIE 15:2004) above 4000 K, " \
    "and on the Planckian (black body) locus (Krystek 1985 approximation) below 4000 K, or on the locus selected by " \
    "the Locus parameter (the daylight locus is defined from 4000 K to 25000 K, and the Planckian approximation from " \
    "1000 K to 15000 K: the temperature is clamped to these domains). The daylight locus is above the Planckian locus, so that the Auto locus jumps by about " \
    "0.003 Duv (0.0028) at 4000 K. 6504 K with no tint on the daylight locus is exactly the D65 white of the " \
    "working spaces (x=0.3127, y=0.3290);\n" \
    "- a tint: a signed distance from the locus (Duv, in the CIE 1960 uv diagram), perpendicular to it. " \
    "Positive values are above the locus (greenish), negative values below the locus (magenta/pinkish). " \
    "A positive source tint thus removes green, in the same direction as a positive Tint in Lightroom or Camera Raw, " \
    "and a positive target tint adds green. For raw files, the Lightroom/Camera Raw Tint is about 3000 times the Duv " \
    "from the Planckian locus (Adobe DNG SDK), e.g. a Source Tint of 0.01 is about +30 on the Planckian locus.\n" \
    "The source white can also be given by picking a neutral color in the image: that color (in the working space) " \
    "is mapped to the target white, so that it becomes neutral (R=G=B) when the target is the working space white. " \
    "Only its chromaticity is used: the luminance (Y) of the neutral color is preserved (it becomes R=G=B with the " \
    "same Y when the target is the working space white).\n" \
    "\n" \
    "The image must be in linear light (scene-linear, not gamma or log encoded), with the primaries and white point " \
    "of the Working Space. The adaptation is a 3x3 matrix, computed once per frame and applied to each pixel: " \
    "XYZ = RGBtoXYZ.RGB, then the cone response LMS = CAT.XYZ (Bradford, CAT02, von Kries, or XYZ scaling), " \
    "each cone response is scaled by the ratio of the target and source white responses, and the result is " \
    "converted back to the working space.\n" \
    "\n" \
    "See also: Grade (whitepoint), ColorMatrix.\n" \
    "\n" \
    "References:\n" \
    "- CIE 15:2004, Colorimetry, 3rd edition (daylight locus).\n" \
    "- J. Krystek, \"An algorithm to calculate correlated colour temperature\", Color Research & Application 10(1), 1985 (Planckian locus in uv).\n" \
    "- Y. Ohno, \"Practical use and calculation of CCT and Duv\", LEUKOS 10(1), 2014 (Duv).\n" \
    "- B. Lindbloom, Chromatic Adaptation, http://www.brucelindbloom.com/Eqn_ChromAdapt.html (Bradford, von Kries, XYZ scaling).\n" \
    "- CIE 159:2004, A colour appearance model for colour management systems: CIECAM02 (CAT02).\n" \
    "- SMPTE RP 177-1993, Derivation of Basic Television Color Equations (RGB to XYZ from the primaries and white)."

#define kPluginIdentifier "net.sf.openfx.WhiteBalance"
// History:
// version 1.0: initial version
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#ifdef OFX_EXTENSIONS_NATRON
#define kParamProcessR kNatronOfxParamProcessR
#define kParamProcessRLabel kNatronOfxParamProcessRLabel
#define kParamProcessRHint kNatronOfxParamProcessRHint
#define kParamProcessG kNatronOfxParamProcessG
#define kParamProcessGLabel kNatronOfxParamProcessGLabel
#define kParamProcessGHint kNatronOfxParamProcessGHint
#define kParamProcessB kNatronOfxParamProcessB
#define kParamProcessBLabel kNatronOfxParamProcessBLabel
#define kParamProcessBHint kNatronOfxParamProcessBHint
#else
#define kParamProcessR      "processR"
#define kParamProcessRLabel "R"
#define kParamProcessRHint  "Process red channel."
#define kParamProcessG      "processG"
#define kParamProcessGLabel "G"
#define kParamProcessGHint  "Process green channel."
#define kParamProcessB      "processB"
#define kParamProcessBLabel "B"
#define kParamProcessBHint  "Process blue channel."
#endif

#define kParamWorkingSpace "workingSpace"
#define kParamWorkingSpaceLabel "Working Space"
#define kParamWorkingSpaceHint "The primaries and white point of the linear RGB input and output images."
#define kParamWorkingSpaceOptionRec709 "Rec. 709 / sRGB", "ITU-R BT.709 / sRGB primaries, D65 white (linear).", "rec709"
#define kParamWorkingSpaceOptionRec2020 "Rec. 2020", "ITU-R BT.2020 primaries, D65 white (linear).", "rec2020"
#define kParamWorkingSpaceOptionACESAP0 "ACES AP0 (ACES2065-1)", "ACES AP0 primaries, ACES white (about D60).", "acesap0"
#define kParamWorkingSpaceOptionACESAP1 "ACEScg AP1", "ACES AP1 primaries (ACEScg), ACES white (about D60).", "acesap1"
#define kParamWorkingSpaceOptionP3D65 "P3-D65", "DCI-P3 primaries, D65 white (as Display P3, but linear).", "p3d65"
#define kParamWorkingSpaceOptionXYZ "XYZ (none)", "The images are CIE 1931 XYZ: no RGB conversion. The working space white (used by the Working Space White target) is then the equal-energy illuminant E (X=Y=Z).", "xyz"

enum WorkingSpaceEnum
{
    eWorkingSpaceRec709 = 0,
    eWorkingSpaceRec2020,
    eWorkingSpaceACESAP0,
    eWorkingSpaceACESAP1,
    eWorkingSpaceP3D65,
    eWorkingSpaceXYZ,
};

#define kParamAdaptation "adaptation"
#define kParamAdaptationLabel "Adaptation"
#define kParamAdaptationHint "The chromatic adaptation transform: the cone response space in which the source white is scaled to the target white."
#define kParamAdaptationOptionBradford "Bradford", "The Bradford transform (Lam 1985), as in ICC profiles and Lindbloom's tables.", "bradford"
#define kParamAdaptationOptionCAT02 "CAT02", "The CAT02 transform of CIECAM02 (CIE 159:2004), with full adaptation.", "cat02"
#define kParamAdaptationOptionVonKries "Von Kries", "The von Kries transform, with the Hunt-Pointer-Estevez cone responses normalized to D65.", "vonkries"
#define kParamAdaptationOptionXYZScaling "XYZ Scaling", "Scale X, Y and Z (the simplest, and least accurate, adaptation).", "xyzscaling"

enum AdaptationEnum
{
    eAdaptationBradford = 0,
    eAdaptationCAT02,
    eAdaptationVonKries,
    eAdaptationXYZScaling,
};

#define kParamLocus "locus"
#define kParamLocusLabel "Locus"
#define kParamLocusHint "The locus of the color temperatures. Note that the daylight locus is above the Planckian locus (by about 0.003 Duv: 0.0028 at 4000 K, 0.0033 at 6504 K), so that the white given by a temperature and tint jumps by about 0.003 Duv at 4000 K with Auto. Choose Planckian or Daylight for a continuous locus."
#define kParamLocusOptionAuto "Auto", "The Planckian locus below 4000 K, the CIE daylight locus above.", "auto"
#define kParamLocusOptionPlanckian "Planckian", "The Planckian (black body) locus, with the approximation of Krystek (1985), valid from 1000 K to 15000 K: higher temperatures give the 15000 K white.", "planckian"
#define kParamLocusOptionDaylight "Daylight", "The CIE daylight locus (CIE 15:2004), defined from 4000 K to 25000 K: lower temperatures give the 4000 K white.", "daylight"

enum LocusEnum
{
    eLocusAuto = 0,
    eLocusPlanckian,
    eLocusDaylight,
};

#define kParamSourceWhite "sourceWhite"
#define kParamSourceWhiteLabel "Source White"
#define kParamSourceWhiteHint "How the source white (the scene illuminant) is given."
#define kParamSourceWhiteOptionTemperature "Temperature/Tint", "The source white is given by Source Temperature and Source Tint.", "temperature"
#define kParamSourceWhiteOptionNeutral "Neutral Color", "The source white is the chromaticity of Neutral Color, e.g. picked from a gray or white object in the image.", "neutral"

enum SourceWhiteEnum
{
    eSourceWhiteTemperature = 0,
    eSourceWhiteNeutral,
};

#define kParamSourceTemperature "sourceTemperature"
#define kParamSourceTemperatureLabel "Source Temperature"
#define kParamSourceTemperatureHint "The correlated color temperature of the scene illuminant, in Kelvin (e.g. 2700 K for incandescent light, 3200 K for tungsten studio light, 5003 K for D50, 6504 K for D65 daylight, 7500 K for shade). The temperature is limited to the domain of the locus: 4000-25000 K for Daylight, 1000-15000 K for Planckian."

#define kParamSourceTint "sourceTint"
#define kParamSourceTintLabel "Source Tint"
#define kParamSourceTintHint "The distance of the scene illuminant from the locus (Duv), perpendicular to it in the CIE 1960 uv diagram. Positive values are greener (above the locus), negative values are more magenta. A positive value removes green from the image, in the same direction as a positive Tint in Lightroom or Camera Raw. For raw files, their Tint is about 3000 x Duv measured from the Planckian locus (Adobe DNG SDK): 0.01 is about +30 with the Planckian locus. The daylight locus (Auto above 4000 K) is about 0.003 above the Planckian one, so that a tint of 0 there is about +9 in Lightroom."

#define kParamNeutral "neutral"
#define kParamNeutralLabel "Neutral Color"
#define kParamNeutralHint "A color of the image (in the working space) that should be neutral, e.g. picked with the color picker of the host on a gray or white object. Only its chromaticity is used, and it is mapped to the target white."

#define kParamTargetWhite "targetWhite"
#define kParamTargetWhiteLabel "Target White"
#define kParamTargetWhiteHint "How the target white is given."
#define kParamTargetWhiteOptionTemperature "Temperature/Tint", "The target white is given by Target Temperature and Target Tint.", "temperature"
#define kParamTargetWhiteOptionWorking "Working Space White", "The target white is the white of the working space (R=G=B), e.g. so that a picked Neutral Color becomes exactly neutral. It is D65 (6504 K with no tint on the daylight locus) for the Rec. 709, Rec. 2020 and P3-D65 spaces, about 6000 K for the ACES spaces, and the equal-energy illuminant E for XYZ.", "working"

enum TargetWhiteEnum
{
    eTargetWhiteTemperature = 0,
    eTargetWhiteWorking,
};

#define kParamTargetTemperature "targetTemperature"
#define kParamTargetTemperatureLabel "Target Temperature"
#define kParamTargetTemperatureHint "The correlated color temperature of the target white, in Kelvin, used when Target White is Temperature/Tint. Lower than Source Temperature makes the image warmer (more orange), higher makes it cooler (bluer). The temperature is limited to the domain of the locus: 4000-25000 K for Daylight, 1000-15000 K for Planckian."

#define kParamTargetTint "targetTint"
#define kParamTargetTintLabel "Target Tint"
#define kParamTargetTintHint "The distance of the target white from the locus (Duv), perpendicular to it in the CIE 1960 uv diagram, used when Target White is Temperature/Tint. Positive values are greener (above the locus), negative values are more magenta: a positive value adds green to the image."

#define kParamPremultChanged "premultChanged"

// D65 temperature: the CIE D65 illuminant has a CCT of 6500*1.4388/1.4380 = 6504 K
// (the value of c2 changed after the definition of the D illuminants).
#define kTemperatureDefault 6504.
// 6504 K with no tint on the daylight locus is the D65 white of the working spaces (ITU-R
// BT.709, BT.2020, SMPTE EG 432-1), x=0.3127, y=0.3290, instead of the daylight locus formula
// (x=0.31271, y=0.32912), so that the default is exactly the identity in these spaces.
#define kD65x 0.3127
#define kD65y 0.3290
#define kTemperatureMin 1000.
#define kTemperatureMax 25000.
#define kTintMax 0.1
// The domain of the Planckian locus approximation (Krystek 1985)
#define kPlanckianMaxTemperature 15000.

// The Auto locus switches from Planckian to daylight at this temperature. CIE 15:2004 defines
// the daylight illuminants from 4000 K, which is also the lower bound of the Daylight locus:
// below about 2244 K, the cubic x_D(T) decreases (x is -1.3 at 1000 K).
#define kDaylightMinTemperature 4000.

////////////////////////////////////////////////////////////////////////////////
// Colorimetry

// CIE 1931 xy -> CIE 1960 UCS uv
static void
xyToUV(double x,
       double y,
       double *u,
       double *v)
{
    double d = -2. * x + 12. * y + 3.;

    *u = 4. * x / d;
    *v = 6. * y / d;
}

// CIE 1960 UCS uv -> CIE 1931 xy. Returns false if the denominator 2u-8v+4 (that is,
// 12(X+Y+Z)/(X+15Y+3Z)) is not positive, which the clamped temperature and tint exclude
// (it is above 0.88 on both loci, with tints of +-0.1).
static bool
uvToXY(double u,
       double v,
       double *x,
       double *y)
{
    double d = 2. * u - 8. * v + 4.;

    if (d <= 1e-6) {
        return false;
    }
    *x = 3. * u / d;
    *y = 2. * v / d;

    return true;
}

// Planckian locus in uv, from J. Krystek, "An algorithm to calculate correlated colour
// temperature", Color Research & Application 10(1):38-40, 1985. Rational Chebyshev
// approximation, valid from 1000 K to 15000 K, |duv| < 8e-5 (see also
// https://en.wikipedia.org/wiki/Planckian_locus#Approximation).
static void
planckianUV(double T,
            double *u,
            double *v)
{
    *u = (0.860117757 + 1.54118254e-4 * T + 1.28641212e-7 * T * T) /
         (1. + 8.42420235e-4 * T + 7.08145163e-7 * T * T);
    *v = (0.317398726 + 4.22806245e-5 * T + 4.20481691e-8 * T * T) /
         (1. - 2.89741816e-5 * T + 1.61456053e-7 * T * T);
}

// CIE daylight locus, CIE 15:2004 "Colorimetry", section 3.1, eq. 3.3 and 3.4, defined from
// 4000 K to 25000 K (see clampWhite()).
static void
daylightUV(double T,
           double *u,
           double *v)
{
    double x;
    double T2 = T * T;
    double T3 = T2 * T;

    if (T <= 7000.) {
        x = -4.6070e9 / T3 + 2.9678e6 / T2 + 0.09911e3 / T + 0.244063;
    } else {
        x = -2.0064e9 / T3 + 1.9018e6 / T2 + 0.24748e3 / T + 0.237040;
    }
    double y = -3.000 * x * x + 2.870 * x - 0.275;
    xyToUV(x, y, u, v);
}

static Point3D
xyToXYZ(double x,
        double y)
{
    // only called with the constant chromaticities of the working spaces and D65 (y != 0)
    return Point3D(x / y, 1., (1. - x - y) / y);
}

// true if T and tint give D65 (see kD65x)
static bool
isD65(double T,
      double tint,
      LocusEnum locus)
{
    return (T == kTemperatureDefault) && (tint == 0.) && (locus != eLocusPlanckian);
}

// Clamp the temperature and tint to the domain of the locus: 1000-25000 K for Auto
// (Planckian below 4000 K, daylight above), 1000-15000 K for Planckian (Krystek 1985),
// 4000-25000 K for Daylight (CIE 15:2004), and +-kTintMax for the tint (a host may not clamp
// the parameter values). These clamps also give finite values for non-finite parameters,
// without testing for NaN: std::min(a, x) and std::max(a, x) return a if x is NaN.
static void
clampWhite(double *T,
           double *tint,
           LocusEnum locus)
{
    double tMin = (locus == eLocusDaylight) ? kDaylightMinTemperature : kTemperatureMin;
    double tMax = (locus == eLocusPlanckian) ? kPlanckianMaxTemperature : kTemperatureMax;

    *T = (std::max)(tMin, (std::min)(tMax, *T));
    *tint = (std::max)(-kTintMax, (std::min)(kTintMax, *tint));
}

// The XYZ (with Y=1) of the white of temperature T and tint (Duv) on the given locus. T and
// tint must have been clamped by clampWhite(). Returns false if it is not a white, which is
// impossible on the clamped domains (the caller then does no correction).
static bool
temperatureToXYZ(double T,
                 double tint,
                 LocusEnum locus,
                 Point3D *xyz)
{
    if ( isD65(T, tint, locus) ) {
        *xyz = xyToXYZ(kD65x, kD65y);

        return true;
    }
    bool daylight = (locus == eLocusDaylight) || (locus == eLocusAuto && T >= kDaylightMinTemperature);
    void (*locusUV)(double, double*, double*) = daylight ? daylightUV : planckianUV;
    double u, v;
    locusUV(T, &u, &v);
    if (tint != 0.) {
        // the tangent of the locus, by a central difference (on the same locus, even near the
        // 4000 K switch of the Auto locus, and slightly outside the domain at its ends)
        double h = T * 1e-4;
        double u1, v1, u2, v2;
        locusUV(T - h, &u1, &v1);
        locusUV(T + h, &u2, &v2);
        double du = u2 - u1;
        double dv = v2 - v1;
        double norm = std::sqrt(du * du + dv * dv);
        if ( !(norm > 0.) ) {
            return false;
        }
        // On the clamped domains, u decreases with T on both loci (du/norm < -0.31), so the
        // normal (dv, -du) points towards +v, i.e. above the locus (towards green), as a
        // positive Duv.
        u += tint * dv / norm;
        v += tint * -du / norm;
    }
    double x, y;
    if ( !uvToXY(u, v, &x, &y) || (y <= 1e-6) ) {
        // impossible on the clamped domains (y > 0.17), but don't divide by zero
        return false;
    }
    *xyz = Point3D(x / y, 1., (1. - x - y) / y);

    return true;
}

// The working spaces whose white is D65 (kD65x, kD65y).
static bool
workingWhiteIsD65(WorkingSpaceEnum space)
{
    return (space == eWorkingSpaceRec709) || (space == eWorkingSpaceRec2020) || (space == eWorkingSpaceP3D65);
}

// The RGB to XYZ matrix of the working space, from its primaries and white chromaticities
// (SMPTE RP 177-1993): the columns are the XYZ of the primaries, scaled so that RGB=(1,1,1)
// is the white with Y=1.
static Matrix3x3
rgbToXYZ(WorkingSpaceEnum space)
{
    double xr, yr, xg, yg, xb, yb, xw, yw;

    switch (space) {
    case eWorkingSpaceRec709:
        // ITU-R BT.709-6
        xr = 0.64; yr = 0.33; xg = 0.30; yg = 0.60; xb = 0.15; yb = 0.06; xw = 0.3127; yw = 0.3290;
        break;
    case eWorkingSpaceRec2020:
        // ITU-R BT.2020-2
        xr = 0.708; yr = 0.292; xg = 0.170; yg = 0.797; xb = 0.131; yb = 0.046; xw = 0.3127; yw = 0.3290;
        break;
    case eWorkingSpaceACESAP0:
        // SMPTE ST 2065-1
        xr = 0.7347; yr = 0.2653; xg = 0.; yg = 1.; xb = 0.0001; yb = -0.0770; xw = 0.32168; yw = 0.33767;
        break;
    case eWorkingSpaceACESAP1:
        // Academy S-2014-004 (ACEScg)
        xr = 0.713; yr = 0.293; xg = 0.165; yg = 0.830; xb = 0.128; yb = 0.044; xw = 0.32168; yw = 0.33767;
        break;
    case eWorkingSpaceP3D65:
        // SMPTE EG 432-1 (P3-D65)
        xr = 0.680; yr = 0.320; xg = 0.265; yg = 0.690; xb = 0.150; yb = 0.060; xw = 0.3127; yw = 0.3290;
        break;
    case eWorkingSpaceXYZ:
    default: {
        Matrix3x3 m;
        m.setIdentity();

        return m;
    }
    }
    Matrix3x3 p( xyToXYZ(xr, yr), xyToXYZ(xg, yg), xyToXYZ(xb, yb) ); // columns
    Matrix3x3 pinv;
    if ( !p.inverse(&pinv) ) {
        assert(false);
        pinv.setIdentity();
    }
    Point3D s = pinv * xyToXYZ(xw, yw);

    return Matrix3x3(p(0, 0) * s.x, p(0, 1) * s.y, p(0, 2) * s.z,
                     p(1, 0) * s.x, p(1, 1) * s.y, p(1, 2) * s.z,
                     p(2, 0) * s.x, p(2, 1) * s.y, p(2, 2) * s.z);
}

// The XYZ to cone response (LMS) matrix of the chromatic adaptation transform.
static Matrix3x3
catMatrix(AdaptationEnum adaptation)
{
    switch (adaptation) {
    case eAdaptationBradford:
        // Lam (1985), as in http://www.brucelindbloom.com/Eqn_ChromAdapt.html
        return Matrix3x3( 0.8951,  0.2664, -0.1614,
                         -0.7502,  1.7135,  0.0367,
                          0.0389, -0.0685,  1.0296);
    case eAdaptationCAT02:
        // CIE 159:2004 (CIECAM02)
        return Matrix3x3( 0.7328,  0.4296, -0.1624,
                         -0.7036,  1.6975,  0.0061,
                          0.0030,  0.0136,  0.9834);
    case eAdaptationVonKries:
        // Hunt-Pointer-Estevez, normalized to D65, as in http://www.brucelindbloom.com/Eqn_ChromAdapt.html
        return Matrix3x3( 0.40024, 0.70760, -0.08081,
                         -0.22630, 1.16532,  0.04570,
                          0.,      0.,       0.91822);
    case eAdaptationXYZScaling:
    default: {
        Matrix3x3 m;
        m.setIdentity();

        return m;
    }
    }
}

////////////////////////////////////////////////////////////////////////////////

class WhiteBalanceProcessorBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg;
    const Image *_maskImg;
    bool _premult;
    int _premultChannel;
    bool _doMasking;
    double _mix;
    bool _maskInvert;
    bool _processR, _processG, _processB;
    float _m[9]; // the RGB to RGB matrix, row-major

public:

    WhiteBalanceProcessorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _srcImg(NULL)
        , _maskImg(NULL)
        , _premult(false)
        , _premultChannel(3)
        , _doMasking(false)
        , _mix(1.)
        , _maskInvert(false)
        , _processR(false)
        , _processG(false)
        , _processB(false)
    {
        std::fill(_m, _m + 9, 0.f);
        _m[0] = _m[4] = _m[8] = 1.f;
    }

    void setSrcImg(const Image *v)
    {
        _srcImg = v;
    }

    void setMaskImg(const Image *v,
                    bool maskInvert)
    {
        _maskImg = v; _maskInvert = maskInvert;
    }

    void doMasking(bool v)
    {
        _doMasking = v;
    }

    void setValues(const Matrix3x3& m,
                   bool premult,
                   int premultChannel,
                   double mix,
                   bool processR,
                   bool processG,
                   bool processB)
    {
        for (int i = 0; i < 9; ++i) {
            _m[i] = (float)m.m[i];
        }
        _premult = premult;
        _premultChannel = premultChannel;
        _mix = mix;
        _processR = processR;
        _processG = processG;
        _processB = processB;
    }
};


template <class PIX, int nComponents, int maxValue>
class WhiteBalanceProcessor
    : public WhiteBalanceProcessorBase
{
public:
    WhiteBalanceProcessor(ImageEffect &instance)
        : WhiteBalanceProcessorBase(instance)
    {
    }

    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        ofxsProcessRGB<nComponents>(*this, _processR, _processG, _processB, procWindow, rs);
    }

public:
    // called by ofxsProcessRGB()
    template<bool processR, bool processG, bool processB>
    void process(const OfxRectI& procWindow, const OfxPointD& rs)
    {
        unused(rs);
        assert(nComponents == 3 || nComponents == 4);
        assert(_dstImg);
        float unpPix[4] = {0.f, 0.f, 0.f, 0.f};
        float tmpPix[4] = {0.f, 0.f, 0.f, 0.f};
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                ofxsUnPremult<PIX, nComponents, maxValue>(srcPix, unpPix, _premult, _premultChannel);
                tmpPix[0] = processR ? (_m[0] * unpPix[0] + _m[1] * unpPix[1] + _m[2] * unpPix[2]) : unpPix[0];
                tmpPix[1] = processG ? (_m[3] * unpPix[0] + _m[4] * unpPix[1] + _m[5] * unpPix[2]) : unpPix[1];
                tmpPix[2] = processB ? (_m[6] * unpPix[0] + _m[7] * unpPix[1] + _m[8] * unpPix[2]) : unpPix[2];
                tmpPix[3] = unpPix[3];
                ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, _premult, _premultChannel, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                // copy back original values from unprocessed channels (and alpha)
                ofxsCopyBackUnprocessedChannels<PIX, nComponents, processR, processG, processB, false>(srcPix, dstPix);
                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    } // process
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class WhiteBalancePlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    WhiteBalancePlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _maskClip(NULL)
        , _processR(NULL)
        , _processG(NULL)
        , _processB(NULL)
        , _workingSpace(NULL)
        , _adaptation(NULL)
        , _locus(NULL)
        , _sourceWhite(NULL)
        , _sourceTemperature(NULL)
        , _sourceTint(NULL)
        , _neutral(NULL)
        , _targetWhite(NULL)
        , _targetTemperature(NULL)
        , _targetTint(NULL)
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
        _processR = fetchBooleanParam(kParamProcessR);
        _processG = fetchBooleanParam(kParamProcessG);
        _processB = fetchBooleanParam(kParamProcessB);
        assert(_processR && _processG && _processB);
        _workingSpace = fetchChoiceParam(kParamWorkingSpace);
        _adaptation = fetchChoiceParam(kParamAdaptation);
        _locus = fetchChoiceParam(kParamLocus);
        _sourceWhite = fetchChoiceParam(kParamSourceWhite);
        _sourceTemperature = fetchDoubleParam(kParamSourceTemperature);
        _sourceTint = fetchDoubleParam(kParamSourceTint);
        _neutral = fetchRGBParam(kParamNeutral);
        _targetWhite = fetchChoiceParam(kParamTargetWhite);
        _targetTemperature = fetchDoubleParam(kParamTargetTemperature);
        _targetTint = fetchDoubleParam(kParamTargetTint);
        assert(_workingSpace && _adaptation && _locus && _sourceWhite && _sourceTemperature && _sourceTint && _neutral &&
               _targetWhite && _targetTemperature && _targetTint);
        _premult = fetchBooleanParam(kParamPremult);
        _premultChannel = fetchChoiceParam(kParamPremultChannel);
        assert(_premult && _premultChannel);
        _mix = fetchDoubleParam(kParamMix);
        _maskApply = ( ofxsMaskIsAlwaysConnected( OFX::getImageEffectHostDescription() ) && paramExists(kParamMaskApply) ) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
        _premultChanged = fetchBooleanParam(kParamPremultChanged);
        assert(_premultChanged);

        updateEnabled();
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(WhiteBalanceProcessorBase &, const RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /** @brief compute the RGB to RGB matrix at the given time. Returns true if it is the identity. */
    bool computeMatrix(double time, Matrix3x3 *m);

    void updateEnabled();

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    Clip *_maskClip;
    BooleanParam* _processR;
    BooleanParam* _processG;
    BooleanParam* _processB;
    ChoiceParam* _workingSpace;
    ChoiceParam* _adaptation;
    ChoiceParam* _locus;
    ChoiceParam* _sourceWhite;
    DoubleParam* _sourceTemperature;
    DoubleParam* _sourceTint;
    RGBParam* _neutral;
    ChoiceParam* _targetWhite;
    DoubleParam* _targetTemperature;
    DoubleParam* _targetTint;
    BooleanParam* _premult;
    ChoiceParam* _premultChannel;
    DoubleParam* _mix;
    BooleanParam* _maskApply;
    BooleanParam* _maskInvert;
    BooleanParam* _premultChanged; // set to true the first time the user connects src
};


bool
WhiteBalancePlugin::computeMatrix(double time,
                                  Matrix3x3 *m)
{
    m->setIdentity();

    WorkingSpaceEnum space = (WorkingSpaceEnum)_workingSpace->getValueAtTime(time);
    AdaptationEnum adaptation = (AdaptationEnum)_adaptation->getValueAtTime(time);
    LocusEnum locus = (LocusEnum)_locus->getValueAtTime(time);
    SourceWhiteEnum sourceWhite = (SourceWhiteEnum)_sourceWhite->getValueAtTime(time);
    TargetWhiteEnum targetWhite = (TargetWhiteEnum)_targetWhite->getValueAtTime(time);
    Matrix3x3 toXYZ = rgbToXYZ(space);
    // the working space white: RGB=(1,1,1), with Y=1
    Point3D working = toXYZ * Point3D(1., 1., 1.);

    // The whites are compared before any computation, so that the identity is exact
    // when they are given by the same values.
    Point3D src, dst;
    bool srcIsWorking = false;
    double srcTemperature = 0., srcTint = 0.;
    if (sourceWhite == eSourceWhiteNeutral) {
        double r, g, b;
        _neutral->getValueAtTime(time, r, g, b);
        if ( (r == g) && (g == b) ) {
            // a gray: the working space white
            srcIsWorking = true;
            src = working;
        } else {
            // Components out of the finite range (the param range is +-DBL_MAX): not a
            // white, no correction. This domain check is done on each component, since
            // std::max may drop a NaN. Like all the finiteness checks of this plug-in, it is
            // only best-effort when built with -ffinite-math-only (e.g. -Ofast), where the
            // compiler may assume that no value is NaN or infinite and remove it.
            if ( !( (std::abs(r) <= DBL_MAX) && (std::abs(g) <= DBL_MAX) && (std::abs(b) <= DBL_MAX) ) ) {
                return true;
            }
            // Only the chromaticity is used: divide by the largest absolute component, so
            // that each component is in [-1,1] and the XYZ conversion can't overflow (tiny
            // picks, even subnormal, are valid). Zero: not a white.
            double s = (std::max)( std::abs(r), (std::max)( std::abs(g), std::abs(b) ) );
            if ( !(s > 0.) ) {
                return true;
            }
            src = toXYZ * Point3D(r / s, g / s, b / s);
            if (src.y <= 1e-10) {
                // zero or negative luminance: not a white, no correction
                return true;
            }
            src.x /= src.y;
            src.z /= src.y;
            src.y = 1.;
        }
    } else {
        srcTemperature = _sourceTemperature->getValueAtTime(time);
        srcTint = _sourceTint->getValueAtTime(time);
        clampWhite(&srcTemperature, &srcTint, locus);
    }
    if (targetWhite == eTargetWhiteWorking) {
        if ( srcIsWorking ||
             ( (sourceWhite == eSourceWhiteTemperature) && workingWhiteIsD65(space) && isD65(srcTemperature, srcTint, locus) ) ) {
            // the source is the working space white (D65 for the D65 spaces)
            return true;
        }
        dst = working;
    } else {
        double dstTemperature = _targetTemperature->getValueAtTime(time);
        double dstTint = _targetTint->getValueAtTime(time);
        clampWhite(&dstTemperature, &dstTint, locus);
        // compared after clamping, so that values that clamp to the same white are an identity
        if ( (sourceWhite == eSourceWhiteTemperature) && (dstTemperature == srcTemperature) && (dstTint == srcTint) ) {
            return true;
        }
        if ( !temperatureToXYZ(dstTemperature, dstTint, locus, &dst) ) {
            return true;
        }
    }
    if ( (sourceWhite == eSourceWhiteTemperature) && !temperatureToXYZ(srcTemperature, srcTint, locus, &src) ) {
        return true;
    }

    Matrix3x3 cat = catMatrix(adaptation);
    // the whites have Y=1, and finite X and Z (the neutral color is normalized above, and
    // its Y is above 1e-10 of its largest component), so the cone responses are finite
    Point3D srcLMS = cat * src;
    Point3D dstLMS = cat * dst;
    // the cone responses of a real white are positive. Picked colors may not be (e.g. a
    // saturated or negative color): don't divide by zero, and no correction.
    if ( (srcLMS.x <= 1e-10) || (srcLMS.y <= 1e-10) || (srcLMS.z <= 1e-10) ||
         (dstLMS.x <= 1e-10) || (dstLMS.y <= 1e-10) || (dstLMS.z <= 1e-10) ) {
        return true;
    }
    Matrix3x3 scale(dstLMS.x / srcLMS.x, 0., 0.,
                    0., dstLMS.y / srcLMS.y, 0.,
                    0., 0., dstLMS.z / srcLMS.z);
    Matrix3x3 catInv, fromXYZ;
    if ( !cat.inverse(&catInv) || !toXYZ.inverse(&fromXYZ) ) {
        return true;
    }
    *m = fromXYZ * catInv * scale * cat * toXYZ;

    return false;
} // WhiteBalancePlugin::computeMatrix

void
WhiteBalancePlugin::updateEnabled()
{
    bool neutral = (SourceWhiteEnum)_sourceWhite->getValue() == eSourceWhiteNeutral;
    bool working = (TargetWhiteEnum)_targetWhite->getValue() == eTargetWhiteWorking;

    _sourceTemperature->setEnabled(!neutral);
    _sourceTint->setEnabled(!neutral);
    _neutral->setEnabled(neutral);
    _targetTemperature->setEnabled(!working);
    _targetTint->setEnabled(!working);
    _locus->setEnabled(!neutral || !working);
}

/* set up and run a processor */
void
WhiteBalancePlugin::setupAndProcess(WhiteBalanceProcessorBase &processor,
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
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    auto_ptr<const Image> mask(doMasking ? _maskClip->fetchImage(time) : 0);
    if (doMasking) {
        if ( mask.get() ) {
            checkBadRenderScaleOrField(mask, args);
        }
        bool maskInvert = _maskInvert->getValueAtTime(time);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );
    processor.setRenderWindow(args.renderWindow, args.renderScale);

    Matrix3x3 m;
    computeMatrix(time, &m);
    bool premult = _premult->getValueAtTime(time);
    int premultChannel = _premultChannel->getValueAtTime(time);
    double mix = _mix->getValueAtTime(time);
    bool processR = _processR->getValueAtTime(time);
    bool processG = _processG->getValueAtTime(time);
    bool processB = _processB->getValueAtTime(time);

    processor.setValues(m, premult, premultChannel, mix,
                        processR, processG, processB);
    processor.process();
} // WhiteBalancePlugin::setupAndProcess

// the overridden render function
void
WhiteBalancePlugin::render(const RenderArguments &args)
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
            WhiteBalanceProcessor<unsigned char, 4, 255> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthUShort: {
            WhiteBalanceProcessor<unsigned short, 4, 65535> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthFloat: {
            WhiteBalanceProcessor<float, 4, 1> fred(*this);
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
            WhiteBalanceProcessor<unsigned char, 3, 255> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthUShort: {
            WhiteBalanceProcessor<unsigned short, 3, 65535> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthFloat: {
            WhiteBalanceProcessor<float, 3, 1> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        default:
            throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
} // WhiteBalancePlugin::render

bool
WhiteBalancePlugin::isIdentity(const IsIdentityArguments &args,
                               Clip * &identityClip,
                               double & /*identityTime*/
                               , int& /*view*/, std::string& /*plane*/)
{
    const double time = args.time;
    double mix = _mix->getValueAtTime(time);

    if (mix == 0.) {
        identityClip = _srcClip;

        return true;
    }

    {
        bool processR = _processR->getValueAtTime(time);
        bool processG = _processG->getValueAtTime(time);
        bool processB = _processB->getValueAtTime(time);
        if (!processR && !processG && !processB) {
            identityClip = _srcClip;

            return true;
        }
    }

    Matrix3x3 m;
    if ( computeMatrix(time, &m) ) {
        identityClip = _srcClip;

        return true;
    }

    if ( ofxsMaskRoDExcludesRenderWindow(args, _maskClip, _maskApply, _maskInvert) ) {
        identityClip = _srcClip;

        return true;
    }

    return false;
} // WhiteBalancePlugin::isIdentity

void
WhiteBalancePlugin::changedClip(const InstanceChangedArgs &args,
                                const std::string &clipName)
{
    ofxsPremultUpdateFromSourceClip(args, clipName, _srcClip, _premult, _premultChanged);
}

void
WhiteBalancePlugin::changedParam(const InstanceChangedArgs &args,
                                 const std::string &paramName)
{
    if ( (paramName == kParamPremult) && (args.reason == eChangeUserEdit) ) {
        _premultChanged->setValue(true);
    } else if ( (paramName == kParamSourceWhite) || (paramName == kParamTargetWhite) ) {
        updateEnabled();
    }
}

mDeclarePluginFactory(WhiteBalancePluginFactory, {ofxsThreadSuiteCheck();}, {});
void
WhiteBalancePluginFactory::describe(ImageEffectDescriptor &desc)
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
    desc.setChannelSelector(ePixelComponentNone); // we have our own channel selector
#endif
}

static void
defineTemperature(ImageEffectDescriptor &desc,
                  PageParamDescriptor *page,
                  const char* name,
                  const char* label,
                  const char* hint)
{
    DoubleParamDescriptor* param = desc.defineDoubleParam(name);

    param->setLabel(label);
    param->setHint(hint);
    param->setRange(kTemperatureMin, kTemperatureMax);
    param->setDisplayRange(2000., 12000.);
    param->setDefault(kTemperatureDefault);
    param->setIncrement(10.);
    param->setDigits(0);
    if (page) {
        page->addChild(*param);
    }
}

static void
defineTint(ImageEffectDescriptor &desc,
           PageParamDescriptor *page,
           const char* name,
           const char* label,
           const char* hint)
{
    DoubleParamDescriptor* param = desc.defineDoubleParam(name);

    param->setLabel(label);
    param->setHint(hint);
    param->setRange(-kTintMax, kTintMax);
    param->setDisplayRange(-0.05, 0.05);
    param->setDefault(0.);
    param->setIncrement(0.001);
    param->setDigits(4);
    if (page) {
        page->addChild(*param);
    }
}

void
WhiteBalancePluginFactory::describeInContext(ImageEffectDescriptor &desc,
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
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessR);
        param->setLabel(kParamProcessRLabel);
        param->setHint(kParamProcessRHint);
        param->setDefault(true);
#ifdef OFX_EXTENSIONS_NUKE
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
#endif
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessG);
        param->setLabel(kParamProcessGLabel);
        param->setHint(kParamProcessGHint);
        param->setDefault(true);
#ifdef OFX_EXTENSIONS_NUKE
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
#endif
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessB);
        param->setLabel(kParamProcessBLabel);
        param->setHint(kParamProcessBHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamWorkingSpace);
        param->setLabel(kParamWorkingSpaceLabel);
        param->setHint(kParamWorkingSpaceHint);
        assert(param->getNOptions() == eWorkingSpaceRec709);
        param->appendOption(kParamWorkingSpaceOptionRec709);
        assert(param->getNOptions() == eWorkingSpaceRec2020);
        param->appendOption(kParamWorkingSpaceOptionRec2020);
        assert(param->getNOptions() == eWorkingSpaceACESAP0);
        param->appendOption(kParamWorkingSpaceOptionACESAP0);
        assert(param->getNOptions() == eWorkingSpaceACESAP1);
        param->appendOption(kParamWorkingSpaceOptionACESAP1);
        assert(param->getNOptions() == eWorkingSpaceP3D65);
        param->appendOption(kParamWorkingSpaceOptionP3D65);
        assert(param->getNOptions() == eWorkingSpaceXYZ);
        param->appendOption(kParamWorkingSpaceOptionXYZ);
        param->setDefault( (int)eWorkingSpaceRec709 );
        if (page) {
            page->addChild(*param);
        }
    }
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamAdaptation);
        param->setLabel(kParamAdaptationLabel);
        param->setHint(kParamAdaptationHint);
        assert(param->getNOptions() == eAdaptationBradford);
        param->appendOption(kParamAdaptationOptionBradford);
        assert(param->getNOptions() == eAdaptationCAT02);
        param->appendOption(kParamAdaptationOptionCAT02);
        assert(param->getNOptions() == eAdaptationVonKries);
        param->appendOption(kParamAdaptationOptionVonKries);
        assert(param->getNOptions() == eAdaptationXYZScaling);
        param->appendOption(kParamAdaptationOptionXYZScaling);
        param->setDefault( (int)eAdaptationBradford );
        if (page) {
            page->addChild(*param);
        }
    }
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamLocus);
        param->setLabel(kParamLocusLabel);
        param->setHint(kParamLocusHint);
        assert(param->getNOptions() == eLocusAuto);
        param->appendOption(kParamLocusOptionAuto);
        assert(param->getNOptions() == eLocusPlanckian);
        param->appendOption(kParamLocusOptionPlanckian);
        assert(param->getNOptions() == eLocusDaylight);
        param->appendOption(kParamLocusOptionDaylight);
        param->setDefault( (int)eLocusAuto );
        if (page) {
            page->addChild(*param);
        }
    }

    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamSourceWhite);
        param->setLabel(kParamSourceWhiteLabel);
        param->setHint(kParamSourceWhiteHint);
        assert(param->getNOptions() == eSourceWhiteTemperature);
        param->appendOption(kParamSourceWhiteOptionTemperature);
        assert(param->getNOptions() == eSourceWhiteNeutral);
        param->appendOption(kParamSourceWhiteOptionNeutral);
        param->setDefault( (int)eSourceWhiteTemperature );
        if (page) {
            page->addChild(*param);
        }
    }
    defineTemperature(desc, page, kParamSourceTemperature, kParamSourceTemperatureLabel, kParamSourceTemperatureHint);
    defineTint(desc, page, kParamSourceTint, kParamSourceTintLabel, kParamSourceTintHint);
    {
        // An ordinary (not colour-managed, kOfxParamPropColourManagement is not set) RGB
        // parameter: its values are those of the image, in the working space, which is
        // what the host's color picker gives when picking in the viewer.
        RGBParamDescriptor* param = desc.defineRGBParam(kParamNeutral);
        param->setLabel(kParamNeutralLabel);
        param->setHint(kParamNeutralHint);
        param->setDefault(1., 1., 1.);
        param->setRange(-DBL_MAX, -DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0., 0., 0., 1., 1., 1.);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamTargetWhite);
        param->setLabel(kParamTargetWhiteLabel);
        param->setHint(kParamTargetWhiteHint);
        assert(param->getNOptions() == eTargetWhiteTemperature);
        param->appendOption(kParamTargetWhiteOptionTemperature);
        assert(param->getNOptions() == eTargetWhiteWorking);
        param->appendOption(kParamTargetWhiteOptionWorking);
        param->setDefault( (int)eTargetWhiteWorking );
        if (page) {
            page->addChild(*param);
        }
    }
    defineTemperature(desc, page, kParamTargetTemperature, kParamTargetTemperatureLabel, kParamTargetTemperatureHint);
    defineTint(desc, page, kParamTargetTint, kParamTargetTintLabel, kParamTargetTintHint);

    ofxsMaskDescribeParams(desc, page);
    ofxsPremultDescribeParams(desc, page);
    ofxsMixDescribeParams(desc, page);

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
} // WhiteBalancePluginFactory::describeInContext

ImageEffect*
WhiteBalancePluginFactory::createInstance(OfxImageEffectHandle handle,
                                          ContextEnum /*context*/)
{
    return new WhiteBalancePlugin(handle);
}

static WhiteBalancePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
