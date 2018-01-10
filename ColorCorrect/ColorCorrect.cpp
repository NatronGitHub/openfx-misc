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
 * OFX ColorCorrect plugin.
 */

#include <cmath>
#include <algorithm>
#include <cfloat> // DBL_MAX

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsCoords.h"
#include "ofxsLut.h"
#include "ofxsMacros.h"
#include "ofxsThreadSuite.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "ColorCorrectOFX"
#define kPluginGrouping "Color"
#define kPluginDescription "Adjusts the saturation, constrast, gamma, gain and offset of an image.\n" \
    "The ranges of the shadows, midtones and highlights are controlled by the curves " \
    "in the \"Ranges\" tab.\n" \
    "The Contrast adjustment works using the formula: Output = (Input/0.18)^Contrast*0.18.\n" \
    "\n" \
    "See also:\n" \
    "- http://opticalenquiry.com/nuke/index.php?title=ColorCorrect\n" \
    "- https://compositormathematic.wordpress.com/2013/07/06/gamma-contrast/"

#define kPluginIdentifier "net.sf.openfx.ColorCorrectPlugin"
// History:
// version 1.0: initial version
// version 2.0: use kNatronOfxParamProcess* parameters
// version 2.1: add range params
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 1 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

////std strings because we need them in changedParam
static const std::string kGroupMaster = std::string("Master");
static const std::string kGroupShadows = std::string("Shadows");
static const std::string kGroupMidtones = std::string("Midtones");
static const std::string kGroupHighlights = std::string("Highlights");
static const std::string kParamEnable = std::string("Enable");
static const std::string kParamSaturation = std::string("Saturation");
static const std::string kParamContrast = std::string("Contrast");
static const std::string kParamGamma = std::string("Gamma");
static const std::string kParamGain = std::string("Gain");
static const std::string kParamOffset = std::string("Offset");

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
#define kParamProcessA kNatronOfxParamProcessA
#define kParamProcessALabel kNatronOfxParamProcessALabel
#define kParamProcessAHint kNatronOfxParamProcessAHint
#else
#define kParamProcessR      "processR"
#define kParamProcessRLabel "R"
#define kParamProcessRHint  "Process red component."
#define kParamProcessG      "processG"
#define kParamProcessGLabel "G"
#define kParamProcessGHint  "Process green component."
#define kParamProcessB      "processB"
#define kParamProcessBLabel "B"
#define kParamProcessBHint  "Process blue component."
#define kParamProcessA      "processA"
#define kParamProcessALabel "A"
#define kParamProcessAHint  "Process alpha component."
#endif

#define kParamRange "range"
#define kParamRangeLabel "Range"
#define kParamRangeHint "Expected range for input values. Within this range, a lookup table is used for faster computation."

#define kParamColorCorrectToneRanges "toneRanges"
#define kParamColorCorrectToneRangesLabel "Tone Ranges"
#define kParamColorCorrectToneRangesHint "Tone ranges lookup table"
#define kParamColorCorrectToneRangesDim0 "Shadow"
#define kParamColorCorrectToneRangesDim1 "Highlight"

#define kParamLuminanceMath "luminanceMath"
#define kParamLuminanceMathLabel "Luminance Math"
#define kParamLuminanceMathHint "Formula used to compute luminance from RGB values (used for saturation adjustments)."
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

#define kParamPremultChanged "premultChanged"

struct ColorControlValues
{
    double r;
    double g;
    double b;
    double a;

    ColorControlValues() : r(0.), g(0.), b(0.), a(0.) {}

    void getValueFrom(double time,
                      RGBAParam* p)
    {
        p->getValueAtTime(time, r, g, b, a);
    }

    void set(double r_,
             double g_,
             double b_,
             double a_)
    {
        r = r_;
        g = g_;
        b = b_;
        a = a_;
    }

    void set(double v)
    {
        r = g = b = a = v;
    }
};

struct ColorControlGroup
{
    ColorControlValues saturation;
    ColorControlValues contrast;
    ColorControlValues gamma;
    ColorControlValues gain;
    ColorControlValues offset;
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

template<bool processR, bool processG, bool processB, bool processA>
struct RGBAPixel
{
    double r, g, b, a;
    LuminanceMathEnum luminanceMath;

    RGBAPixel(double r_,
              double g_,
              double b_,
              double a_,
              LuminanceMathEnum luminanceMath_)
        : r(r_)
        , g(g_)
        , b(b_)
        , a(a_)
        , luminanceMath(luminanceMath_)
    {
    }

    void applySMH(const ColorControlGroup& sValues,
                  double s_scale,
                  const ColorControlGroup& mValues,
                  double m_scale,
                  const ColorControlGroup& hValues,
                  double h_scale,
                  const ColorControlGroup& masterValues)
    {
        RGBAPixel s = *this;
        RGBAPixel m = *this;
        RGBAPixel h = *this;

        s.applyGroup(sValues);
        m.applyGroup(mValues);
        h.applyGroup(hValues);

        if (processR) {
            r = s.r * s_scale + m.r * m_scale + h.r * h_scale;
        }
        if (processG) {
            g = s.g * s_scale + m.g * m_scale + h.g * h_scale;
        }
        if (processB) {
            b = s.b * s_scale + m.b * m_scale + h.b * h_scale;
        }
        if (processA) {
            a = s.a * s_scale + m.a * m_scale + h.a * h_scale;
        }
        applyGroup(masterValues);
    }

private:
    void applySaturation(const ColorControlValues &c)
    {
        if (!(processR && c.r != 1.) &&
            !(processG && c.g != 1.) &&
            !(processB && c.b != 1.)) {

            return;
        }
        double l = luminance(r, g, b, luminanceMath);
        if (processR && c.r != 1.) {
            r = (1.f - c.r) * l + c.r * r;
        }
        if (processG && c.g != 1.) {
            g = (1.f - c.g) * l + c.g * g;
        }
        if (processB && c.b != 1.) {
            b = (1.f - c.b) * l + c.b * b;
        }
    }

    void applyContrast(const ColorControlValues &c)
    {
        // See https://compositormathematic.wordpress.com/2013/07/06/gamma-contrast/
        // 0.18 is the value that a (maybe) correctly exposed grey card is in sRGB
        // colour space. A grey card is a piece of card who’s surface is specially
        // designed to reflect 18% of the light that hits it. It’s used in
        // photography alongside a light meter to judge the correct exposure of a
        // scene. The argument is that for some reason 18% is the value of middle
        // grey, and all fingers seem to point to a photographer named Ansel
        // Adams who somehow convinced the people at Kodak of this. You can read
        // about it here: http://bythom.com/graycards.htm. People in the know say
        // that this value of 18% is about 1/2 a stop wrong, and it should be more
        // like 12%. It would also seem that the people making grey cards aren’t
        // talking to the people making light meters.

        if (processR && (r > 0) && c.r != 1.) {
            r = std::pow(r / 0.18, c.r) * 0.18;
        }
        if (processG && (g > 0) && c.g != 1.) {
            g = std::pow(g / 0.18, c.g) * 0.18;
        }
        if (processB && (b > 0) && c.b != 1.) {
            b = std::pow(b / 0.18, c.b) * 0.18;
        }
        if (processA && (a > 0) && c.a != 1.) {
            a = std::pow(a / 0.18, c.a) * 0.18;
        }
    }

    void applyGain(const ColorControlValues &c)
    {
        if (processR && c.r != 1.) {
            r = r * c.r;
        }
        if (processG && c.g != 1.) {
            g = g * c.g;
        }
        if (processB && c.b != 1.) {
            b = b * c.b;
        }
        if (processA && c.a != 1.) {
            a = a * c.a;
        }
    }

    void applyGamma(const ColorControlValues &c)
    {
        if ( processR && (r > 0) && c.r != 1. ) {
            r = std::pow(r, 1. / c.r);
        }
        if ( processG && (g > 0) && c.g != 1. ) {
            g = std::pow(g, 1. / c.g);
        }
        if ( processB && (b > 0) && c.b != 1. ) {
            b = std::pow(b, 1. / c.b);
        }
        if ( processA && (a > 0) && c.a != 1. ) {
            a = std::pow(a, 1. / c.a);
        }
    }

    void applyOffset(const ColorControlValues &c)
    {
        if (processR && c.r != 0.) {
            r = r + c.r;
        }
        if (processG && c.g != 0.) {
            g = g + c.g;
        }
        if (processB && c.b != 0.) {
            b = b + c.b;
        }
        if (processA && c.a != 0.) {
            a = a + c.a;
        }
    }

    void applyGroup(const ColorControlGroup& group)
    {
        applySaturation(group.saturation);
        applyContrast(group.contrast);
        applyGamma(group.gamma);
        applyGain(group.gain);
        applyOffset(group.offset);
    }
};

class ColorCorrecterBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg;
    const Image *_maskImg;
    bool _premult;
    int _premultChannel;
    bool _doMasking;
    const bool _clampBlack;
    const bool _clampWhite;
    double _mix;
    bool _maskInvert;
    bool _processR, _processG, _processB, _processA;

public:
    ColorCorrecterBase(ImageEffect &instance,
                       bool clampBlack,
                       bool clampWhite,
                       const RenderArguments & /*args*/)
        : ImageProcessor(instance)
        , _srcImg(NULL)
        , _maskImg(NULL)
        , _premult(false)
        , _premultChannel(3)
        , _doMasking(false)
        , _clampBlack(clampBlack)
        , _clampWhite(clampWhite)
        , _mix(1.)
        , _maskInvert(false)
        , _processR(false)
        , _processG(false)
        , _processB(false)
        , _processA(false)
        , _luminanceMath(eLuminanceMathRec709)
    {
    }

    void setSrcImg(const Image *v) {_srcImg = v; }

    void setMaskImg(const Image *v,
                    bool maskInvert) {_maskImg = v; _maskInvert = maskInvert; }

    void doMasking(bool v) {_doMasking = v; }

    void setColorControlValues(const ColorControlGroup& master,
                               const ColorControlGroup& shadow,
                               const ColorControlGroup& midtone,
                               const ColorControlGroup& hightlights,
                               LuminanceMathEnum luminanceMath,
                               bool premult,
                               int premultChannel,
                               double mix,
                               bool processR,
                               bool processG,
                               bool processB,
                               bool processA)
    {
        _masterValues = master;
        _shadowValues = shadow;
        _midtoneValues = midtone;
        _highlightsValues = hightlights;
        _luminanceMath = luminanceMath;
        _premult = premult;
        _premultChannel = premultChannel;
        _mix = mix;
        _processR = processR;
        _processG = processG;
        _processB = processB;
        _processA = processA;
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

protected:
    ColorControlGroup _masterValues;
    ColorControlGroup _shadowValues;
    ColorControlGroup _midtoneValues;
    ColorControlGroup _highlightsValues;
    LuminanceMathEnum _luminanceMath;
};

// floats don't clamp except if _clampBlack or _clampWhite
template<>
float
ColorCorrecterBase::clamp<float>(float value,
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
ColorCorrecterBase::clamp<float>(double value,
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

// template to do the processing.
// nbValues is the number of values in the LUT minus 1. For integer types, it should be the same as
// maxValue
template <class PIX, int nComponents, int maxValue, int nbValues>
class ColorCorrecter
    : public ColorCorrecterBase
{
public:
    ColorCorrecter(ImageEffect &instance,
                   const RenderArguments &args,
                   ParametricParam  *lookupTableParam,
                   double rangeMin,
                   double rangeMax,
                   bool clampBlack,
                   bool clampWhite)
        : ColorCorrecterBase(instance, clampBlack, clampWhite, args)
        , _lookupTableParam(lookupTableParam)
        , _rangeMin( std::min(rangeMin, rangeMax) )
        , _rangeMax( std::max(rangeMin, rangeMax) )
    {
        _time = args.time;
        // build the LUT
        if (_rangeMin == _rangeMax) {
            // avoid divisions by zero
            _rangeMax = _rangeMin + 1.;
        }
        assert( (PIX)maxValue == maxValue );
        // except for float, maxValue is the same as nbValues
        assert( maxValue == 1 || (maxValue == nbValues) );
        for (int curve = 0; curve < 2; ++curve) {
            _lookupTable[curve].resize(nbValues + 1);
            for (int position = 0; position <= nbValues; ++position) {
                // position to evaluate the param at
                double parametricPos = _rangeMin + (_rangeMax - _rangeMin) * double(position) / nbValues;

                // evaluate the parametric param
                double value;
                if (_lookupTableParam) {
                    value = _lookupTableParam->getValue(curve, _time, parametricPos);
                } else if (curve == 0) {
                    if (parametricPos <= 0.) {
                        value = 1.;
                    } else if (parametricPos < 0.09) {
                        double x = parametricPos / 0.09;
                        x = -2 * x * x * x + 3 * x * x; // cubic
                        value = 1. - x;
                    } else {
                        value = 0.;
                    }
                } else {
                    assert(curve == 1);
                    if (parametricPos <= 0.5) {
                        value = 0.;
                    } else if (parametricPos >= 1.) {
                        value = 1.;
                    } else {
                        double x = (parametricPos - 0.5) / 0.5;
                        x = -2 * x * x * x + 3 * x * x; // cubic
                        value = x;
                    }
                }
                // set that in the lut
                _lookupTable[curve][position] = (float)clamp<PIX>(value, maxValue);
            }
        }
    }

    void multiThreadProcessImages(OfxRectI procWindow)
    {
#     ifndef __COVERITY__ // too many coverity[dead_error_line] errors
        const bool r = _processR && (nComponents != 1);
        const bool g = _processG && (nComponents >= 2);
        const bool b = _processB && (nComponents >= 3);
        const bool a = _processA && (nComponents == 1 || nComponents == 4);
        if (r) {
            if (g) {
                if (b) {
                    if (a) {
                        return process<true, true, true, true >(procWindow); // RGBA
                    } else {
                        return process<true, true, true, false>(procWindow); // RGBa
                    }
                } else {
                    if (a) {
                        return process<true, true, false, true >(procWindow); // RGbA
                    } else {
                        return process<true, true, false, false>(procWindow); // RGba
                    }
                }
            } else {
                if (b) {
                    if (a) {
                        return process<true, false, true, true >(procWindow); // RgBA
                    } else {
                        return process<true, false, true, false>(procWindow); // RgBa
                    }
                } else {
                    if (a) {
                        return process<true, false, false, true >(procWindow); // RgbA
                    } else {
                        return process<true, false, false, false>(procWindow); // Rgba
                    }
                }
            }
        } else {
            if (g) {
                if (b) {
                    if (a) {
                        return process<false, true, true, true >(procWindow); // rGBA
                    } else {
                        return process<false, true, true, false>(procWindow); // rGBa
                    }
                } else {
                    if (a) {
                        return process<false, true, false, true >(procWindow); // rGbA
                    } else {
                        return process<false, true, false, false>(procWindow); // rGba
                    }
                }
            } else {
                if (b) {
                    if (a) {
                        return process<false, false, true, true >(procWindow); // rgBA
                    } else {
                        return process<false, false, true, false>(procWindow); // rgBa
                    }
                } else {
                    if (a) {
                        return process<false, false, false, true >(procWindow); // rgbA
                    } else {
                        return process<false, false, false, false>(procWindow); // rgba
                    }
                }
            }
        }
#     endif // ifndef __COVERITY__
    } // multiThreadProcessImages

private:


    template<bool processR, bool processG, bool processB, bool processA>
    void process(OfxRectI procWindow)
    {
        assert( (!processR && !processG && !processB) || (nComponents == 3 || nComponents == 4) );
        assert( !processA || (nComponents == 1 || nComponents == 4) );
        assert(nComponents == 3 || nComponents == 4);
        float unpPix[4];
        float tmpPix[4];
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                ofxsUnPremult<PIX, nComponents, maxValue>(srcPix, unpPix, _premult, _premultChannel);
                double t_r = unpPix[0];
                double t_g = unpPix[1];
                double t_b = unpPix[2];
                double t_a = unpPix[3];
                colorTransform<processR, processG, processB, processA>(&t_r, &t_g, &t_b, &t_a);
                tmpPix[0] = (float)t_r;
                tmpPix[1] = (float)t_g;
                tmpPix[2] = (float)t_b;
                tmpPix[3] = (float)t_a;
                ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, _premult, _premultChannel, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                // copy back original values from unprocessed channels
                if (nComponents == 1) {
                    if (!processA) {
                        dstPix[0] = srcPix ? srcPix[0] : PIX();
                    }
                } else if ( (nComponents == 3) || (nComponents == 4) ) {
                    if (!processR) {
                        dstPix[0] = srcPix ? srcPix[0] : PIX();
                    }
                    if (!processG) {
                        dstPix[1] = srcPix ? srcPix[1] : PIX();
                    }
                    if (!processB) {
                        dstPix[2] = srcPix ? srcPix[2] : PIX();
                    }
                    if ( !processA && (nComponents == 4) ) {
                        dstPix[3] = srcPix ? srcPix[3] : PIX();
                    }
                }
                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    } // process

    template<bool processR, bool processG, bool processB, bool processA>
    void colorTransform(double *r,
                        double *g,
                        double *b,
                        double *a)
    {
        double l = luminance(*r, *g, *b, _luminanceMath);
        double s_scale = interpolate(0, l);
        double h_scale = interpolate(1, l);
        double m_scale = 1.f - s_scale - h_scale;

        RGBAPixel<processR, processG, processB, processA> p(*r, *g, *b, *a, _luminanceMath);
        p.applySMH(_shadowValues, s_scale,
                   _midtoneValues, m_scale,
                   _highlightsValues, h_scale,
                   _masterValues);
        if (processR) {
            *r = clamp<float>(p.r, 1);
        }
        if (processG) {
            *g = clamp<float>(p.g, 1);
        }
        if (processB) {
            *b = clamp<float>(p.b, 1);
        }
        if (processA) {
            *a = clamp<float>(p.a, 1);
        }
    }

    // on input to interpolate, value should be normalized to the [0-1] range
    float interpolate(int component,
                      float value) const
    {
        if ( (value < _rangeMin) || (_rangeMax < value) ) {
            // slow version
            double ret = _lookupTableParam->getValue(component, _time, value);

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

    std::vector<float> _lookupTable[2];
    ParametricParam*  _lookupTableParam;
    double _time;
    double _rangeMin;
    double _rangeMax;
};

struct ColorControlParamGroup
{
    ColorControlParamGroup()
        : enable(NULL)
        , saturation(NULL)
        , contrast(NULL)
        , gamma(NULL)
        , gain(NULL)
        , offset(NULL) {}

    BooleanParam* enable;
    RGBAParam* saturation;
    RGBAParam* contrast;
    RGBAParam* gamma;
    RGBAParam* gain;
    RGBAParam* offset;
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ColorCorrectPlugin
    : public ImageEffect
{
public:

    enum ColorCorrectGroupType
    {
        eGroupMaster = 0,
        eGroupShadow,
        eGroupMidtone,
        eGroupHighlight
    };

    /** @brief ctor */
    ColorCorrectPlugin(OfxImageEffectHandle handle,
                       bool supportsParametricParameter)
        : ImageEffect(handle)
        , _supportsParametricParameter(supportsParametricParameter)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _maskClip(NULL)
        , _processR(NULL)
        , _processG(NULL)
        , _processB(NULL)
        , _processA(NULL)
        , _range(NULL)
        , _rangesParam(NULL)
        , _luminanceMath(NULL)
        , _clampBlack(NULL)
        , _clampWhite(NULL)
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
        fetchColorControlGroup(kGroupMaster, &_masterParamsGroup);
        fetchColorControlGroup(kGroupShadows, &_shadowsParamsGroup);
        fetchColorControlGroup(kGroupMidtones, &_midtonesParamsGroup);
        fetchColorControlGroup(kGroupHighlights, &_highlightsParamsGroup);
        _range = fetchDouble2DParam(kParamRange);
        assert(_range);
        if (_supportsParametricParameter) {
            _rangesParam = fetchParametricParam(kParamColorCorrectToneRanges);
            assert(_rangesParam);
        }
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

        _processR = fetchBooleanParam(kNatronOfxParamProcessR);
        _processG = fetchBooleanParam(kNatronOfxParamProcessG);
        _processB = fetchBooleanParam(kNatronOfxParamProcessB);
        _processA = fetchBooleanParam(kNatronOfxParamProcessA);
        assert(_processR && _processG && _processB && _processA);
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(ColorCorrecterBase &, const RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;
    void fetchColorControlGroup(const std::string& groupName,
                                ColorControlParamGroup* group)
    {
        assert(group);
        group->enable = (groupName == kGroupMaster) ? 0 : fetchBooleanParam(groupName  + kParamEnable);
        group->saturation = fetchRGBAParam(groupName  + kParamSaturation);
        group->contrast = fetchRGBAParam(groupName +  kParamContrast);
        group->gamma = fetchRGBAParam(groupName  + kParamGamma);
        group->gain = fetchRGBAParam(groupName + kParamGain);
        group->offset = fetchRGBAParam(groupName + kParamOffset);
        assert(group->saturation && group->contrast && group->gamma && group->gain && group->offset);
    }

    void getColorCorrectGroupValues(double time, ColorControlGroup* groupValues, ColorCorrectGroupType type);

    ColorControlParamGroup& getGroup(ColorCorrectGroupType type)
    {
        switch (type) {
        case eGroupMaster:

            return _masterParamsGroup;
        case eGroupShadow:

            return _shadowsParamsGroup;
        case eGroupMidtone:

            return _midtonesParamsGroup;
        case eGroupHighlight:

            return _highlightsParamsGroup;
        default:
            assert(false);
            break;
        }
    }

private:
    bool _supportsParametricParameter;
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    Clip *_maskClip;
    ColorControlParamGroup _masterParamsGroup;
    ColorControlParamGroup _shadowsParamsGroup;
    ColorControlParamGroup _midtonesParamsGroup;
    ColorControlParamGroup _highlightsParamsGroup;
    BooleanParam* _processR;
    BooleanParam* _processG;
    BooleanParam* _processB;
    BooleanParam* _processA;
    Double2DParam* _range;
    ParametricParam* _rangesParam;
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
ColorCorrectPlugin::getColorCorrectGroupValues(double time,
                                               ColorControlGroup* groupValues,
                                               ColorCorrectGroupType type)
{
    ColorControlParamGroup& group = getGroup(type);
    bool enable = true;

    if (group.enable) {
        group.enable->getValueAtTime(time, enable);
    }
    if (enable) {
        groupValues->saturation.getValueFrom(time, group.saturation);
        groupValues->contrast.getValueFrom(time, group.contrast);
        groupValues->gamma.getValueFrom(time, group.gamma);
        groupValues->gain.getValueFrom(time, group.gain);
        groupValues->offset.getValueFrom(time, group.offset);
    } else {
        groupValues->saturation.set(1.);
        groupValues->contrast.set(1.);
        groupValues->gamma.set(1.);
        groupValues->gain.set(1.);
        groupValues->offset.set(0.);
    }
}

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
ColorCorrectPlugin::setupAndProcess(ColorCorrecterBase &processor,
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
    if ( mask.get() ) {
        if ( (mask->getRenderScale().x != args.renderScale.x) ||
             ( mask->getRenderScale().y != args.renderScale.y) ||
             ( ( mask->getField() != eFieldNone) /* for DaVinci Resolve */ && ( mask->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
    }
    if (doMasking) {
        bool maskInvert = _maskInvert->getValueAtTime(time);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );
    processor.setRenderWindow(args.renderWindow);

    ColorControlGroup masterValues, shadowValues, midtoneValues, highlightValues;
    getColorCorrectGroupValues(time, &masterValues,    eGroupMaster);
    getColorCorrectGroupValues(time, &shadowValues,    eGroupShadow);
    getColorCorrectGroupValues(time, &midtoneValues,   eGroupMidtone);
    getColorCorrectGroupValues(time, &highlightValues, eGroupHighlight);
    LuminanceMathEnum luminanceMath = (LuminanceMathEnum)_luminanceMath->getValueAtTime(time);
    bool premult = _premult->getValueAtTime(time);
    int premultChannel = _premultChannel->getValueAtTime(time);
    double mix = _mix->getValueAtTime(time);
    bool processR = _processR->getValueAtTime(time);
    bool processG = _processG->getValueAtTime(time);
    bool processB = _processB->getValueAtTime(time);
    bool processA = _processA->getValueAtTime(time);

    processor.setColorControlValues(masterValues, shadowValues, midtoneValues, highlightValues, luminanceMath, premult, premultChannel, mix,
                                    processR, processG, processB, processA);
    processor.process();
} // ColorCorrectPlugin::setupAndProcess

// the overridden render function
void
ColorCorrectPlugin::render(const RenderArguments &args)
{
    //std::cout << "render!\n";
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(dstComponents == ePixelComponentRGB || dstComponents == ePixelComponentRGBA);
    double rangeMin, rangeMax;
    const double time = args.time;

    _range->getValueAtTime(time, rangeMin, rangeMax);
    bool clampBlack = _clampBlack->getValueAtTime(time);
    bool clampWhite = _clampWhite->getValueAtTime(time);

    if (dstComponents == ePixelComponentRGBA) {
        switch (dstBitDepth) {
        case eBitDepthUByte: {
            ColorCorrecter<unsigned char, 4, 255, 255> fred(*this, args, _rangesParam, rangeMin, rangeMax, clampBlack, clampWhite);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthUShort: {
            ColorCorrecter<unsigned short, 4, 65535, 65535> fred(*this, args, _rangesParam, rangeMin, rangeMax, clampBlack, clampWhite);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthFloat: {
            ColorCorrecter<float, 4, 1, 1023> fred(*this, args, _rangesParam, rangeMin, rangeMax, clampBlack, clampWhite);
            setupAndProcess(fred, args);
            break;
        }
        default:
            //std::cout << "depth usupported\n";
            throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else {
        assert(dstComponents == ePixelComponentRGB);
        switch (dstBitDepth) {
        case eBitDepthUByte: {
            ColorCorrecter<unsigned char, 3, 255, 255> fred(*this, args, _rangesParam, rangeMin, rangeMax, clampBlack, clampWhite);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthUShort: {
            ColorCorrecter<unsigned short, 3, 65535, 65535> fred(*this, args, _rangesParam, rangeMin, rangeMax, clampBlack, clampWhite);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthFloat: {
            ColorCorrecter<float, 3, 1, 1023> fred(*this, args, _rangesParam, rangeMin, rangeMax, clampBlack, clampWhite);
            setupAndProcess(fred, args);
            break;
        }
        default:
            //std::cout << "components usupported\n";
            throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
    //std::cout << "render! OK\n";
} // ColorCorrectPlugin::render

static bool
groupIsIdentity(const ColorControlGroup& group)
{
    return (group.saturation.r == 1. &&
            group.saturation.g == 1. &&
            group.saturation.b == 1. &&
            group.saturation.a == 1. &&
            group.contrast.r == 1. &&
            group.contrast.g == 1. &&
            group.contrast.b == 1. &&
            group.contrast.a == 1. &&
            group.gamma.r == 1. &&
            group.gamma.g == 1. &&
            group.gamma.b == 1. &&
            group.gamma.a == 1. &&
            group.gain.r == 1. &&
            group.gain.g == 1. &&
            group.gain.b == 1. &&
            group.gain.a == 1. &&
            group.offset.r == 0. &&
            group.offset.g == 0. &&
            group.offset.b == 0. &&
            group.offset.a == 0.);
}

bool
ColorCorrectPlugin::isIdentity(const IsIdentityArguments &args,
                               Clip * &identityClip,
                               double & /*identityTime*/
                            , int& /*view*/, std::string& /*plane*/)
{
    //std::cout << "isIdentity!\n";
    double mix;

    _mix->getValueAtTime(args.time, mix);

    if (mix == 0. /*|| (!processR && !processG && !processB && !processA)*/) {
        identityClip = _srcClip;

        return true;
    }

    {
        bool processR;
        bool processG;
        bool processB;
        bool processA;
        _processR->getValueAtTime(args.time, processR);
        _processG->getValueAtTime(args.time, processG);
        _processB->getValueAtTime(args.time, processB);
        _processA->getValueAtTime(args.time, processA);
        if (!processR && !processG && !processB && !processA) {
            identityClip = _srcClip;

            return true;
        }
    }

    bool clampBlack, clampWhite;
    _clampBlack->getValueAtTime(args.time, clampBlack);
    _clampWhite->getValueAtTime(args.time, clampWhite);
    if (clampBlack || clampWhite) {
        return false;
    }

    ColorControlGroup masterValues, shadowValues, midtoneValues, highlightValues;
    getColorCorrectGroupValues(args.time, &masterValues,    eGroupMaster);
    getColorCorrectGroupValues(args.time, &shadowValues,    eGroupShadow);
    getColorCorrectGroupValues(args.time, &midtoneValues,   eGroupMidtone);
    getColorCorrectGroupValues(args.time, &highlightValues, eGroupHighlight);
    if ( groupIsIdentity(masterValues) &&
         groupIsIdentity(shadowValues) &&
         groupIsIdentity(midtoneValues) &&
         groupIsIdentity(highlightValues) ) {
        identityClip = _srcClip;

        //std::cout << "isIdentity! true\n";
        return true;
    }

    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(args.time) ) && _maskClip && _maskClip->isConnected() );
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        if (!maskInvert) {
            OfxRectI maskRoD;
            if (getImageEffectHostDescription()->supportsMultiResolution) {
                // In Sony Catalyst Edit, clipGetRegionOfDefinition returns the RoD in pixels instead of canonical coordinates.
                // In hosts that do not support multiResolution (e.g. Sony Catalyst Edit), all inputs have the same RoD anyway.
                Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(args.time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
                // effect is identity if the renderWindow doesn't intersect the mask RoD
                if ( !Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0) ) {
                    identityClip = _srcClip;

                    return true;
                }
            }
        }
    }

    //std::cout << "isIdentity! false\n";
    return false;
} // ColorCorrectPlugin::isIdentity

void
ColorCorrectPlugin::changedClip(const InstanceChangedArgs &args,
                                const std::string &clipName)
{
    //std::cout << "changedClip!\n";
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
    //std::cout << "changedClip OK!\n";
}

void
ColorCorrectPlugin::changedParam(const InstanceChangedArgs &args,
                                 const std::string &paramName)
{
    const double time = args.time;

    if ( (paramName == kParamRange) && (args.reason == eChangeUserEdit) ) {
        double rmin, rmax;
        _range->getValueAtTime(time, rmin, rmax);
        if (rmax < rmin) {
            _range->setValue(rmax, rmin);
        }
    } else if ( (paramName == kParamPremult) && (args.reason == eChangeUserEdit) ) {
        _premultChanged->setValue(true);
    }
}

mDeclarePluginFactory(ColorCorrectPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
ColorCorrectPluginFactory::describe(ImageEffectDescriptor &desc)
{
    //std::cout << "describe!\n";
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
    //std::cout << "describe! OK\n";

#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone); // we have our own channel selector
#endif
}

static void
defineRGBAScaleParam(ImageEffectDescriptor &desc,
                     const std::string &name,
                     const std::string &label,
                     const std::string &hint,
                     GroupParamDescriptor *parent,
                     PageParamDescriptor* page,
                     double def,
                     double min,
                     double max)
{
    RGBAParamDescriptor *param = desc.defineRGBAParam(name);

    param->setLabel(label);
    param->setHint(hint);
    param->setDefault(def, def, def, def);
    param->setRange(-DBL_MAX, -DBL_MAX, -DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
    param->setDisplayRange(min, min, min, min, max, max, max, max);
    if (parent) {
        param->setParent(*parent);
    }
    if (page) {
        page->addChild(*param);
    }
}

static void
defineColorGroup(const std::string& groupName,
                 const std::string& hint,
                 PageParamDescriptor* page,
                 ImageEffectDescriptor &desc,
                 bool open)
{
    GroupParamDescriptor* group = desc.defineGroupParam(groupName);

    if (group) {
        group->setLabel(groupName);
        group->setHint(hint);
        group->setOpen(open);
        if (page) {
            page->addChild(*group);
        }
    }

    if (groupName != kGroupMaster) {
        BooleanParamDescriptor *param = desc.defineBooleanParam(groupName + kParamEnable);
        param->setLabel(kParamEnable);
        param->setHint("When checked, " + groupName + " correction is enabled.");
        param->setDefault(true);
        if (group) {
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    defineRGBAScaleParam(desc, groupName + kParamSaturation, kParamSaturation, hint, group, page, 1, 0, 4);
    defineRGBAScaleParam(desc, groupName + kParamContrast,   kParamContrast,   hint, group, page, 1, 0, 4);
    defineRGBAScaleParam(desc, groupName + kParamGamma,      kParamGamma,      hint, group, page, 1, 0.2, 5);
    defineRGBAScaleParam(desc, groupName + kParamGain,       kParamGain,       hint, group, page, 1, 0, 4);
    defineRGBAScaleParam(desc, groupName + kParamOffset,     kParamOffset,     hint, group, page, 0, -1, 1);
}

void
ColorCorrectPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                             ContextEnum context)
{
    //std::cout << "describeInContext!\n";
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
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessG);
        param->setLabel(kParamProcessGLabel);
        param->setHint(kParamProcessGHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessB);
        param->setLabel(kParamProcessBLabel);
        param->setHint(kParamProcessBHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessA);
        param->setLabel(kParamProcessALabel);
        param->setHint(kParamProcessAHint);
        param->setDefault(false);
        if (page) {
            page->addChild(*param);
        }
    }

    defineColorGroup(kGroupMaster, "", page, desc, true);
    defineColorGroup(kGroupShadows, "", page, desc, false);
    defineColorGroup(kGroupMidtones, "", page, desc, false);
    defineColorGroup(kGroupHighlights, "", page, desc, false);

    {
        PageParamDescriptor* ranges = desc.definePageParam("Ranges");
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
            if (ranges) {
                ranges->addChild(*param);
            }
        }
        const ImageEffectHostDescription &gHostDescription = *getImageEffectHostDescription();
        const bool supportsParametricParameter = ( gHostDescription.supportsParametricParameter &&
                                                  !(gHostDescription.hostName == "uk.co.thefoundry.nuke" &&
                                                    8 <= gHostDescription.versionMajor && gHostDescription.versionMajor <= 10) );  // Nuke 8-10 are known to *not* support Parametric
        if (supportsParametricParameter) {
            ParametricParamDescriptor* param = desc.defineParametricParam(kParamColorCorrectToneRanges);
            assert(param);
            param->setLabel(kParamColorCorrectToneRangesLabel);
            param->setHint(kParamColorCorrectToneRangesHint);
            
            // define it as two dimensional
            param->setDimension(2);
            
            param->setDimensionLabel(kParamColorCorrectToneRangesDim0, 0);
            param->setDimensionLabel(kParamColorCorrectToneRangesDim1, 1);
            
            // set the UI colour for each dimension
            const OfxRGBColourD shadow   = {0.6, 0.4, 0.6};
            const OfxRGBColourD highlight  =  {0.8, 0.7, 0.6};
            param->setUIColour( 0, shadow );
            param->setUIColour( 1, highlight );
            
            // set the min/max parametric range to 0..1
            param->setRange(0.0, 1.0);
            // set the default Y range to 0..1 for all dimensions
            param->setDimensionDisplayRange(0., 1., 0);
            param->setDimensionDisplayRange(0., 1., 1);
            
            param->addControlPoint(0, // curve to set
                                   0.0,         // time, ignored in this case, as we are not adding a key
                                   0.0,         // parametric position, zero
                                   1.0,         // value to be, 0
                                   false);         // don't add a key
            param->addControlPoint(0, 0.0, 0.09, 0.0, false);
            
            param->addControlPoint(1, 0.0, 0.5, 0.0, false);
            param->addControlPoint(1, 0.0, 1.0, 1.0, false);
            if (ranges) {
                ranges->addChild(*param);
            }
        }
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

    ofxsPremultDescribeParams(desc, page);
    ofxsMaskMixDescribeParams(desc, page);
    //std::cout << "describeInCotext! OK\n";

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
} // ColorCorrectPluginFactory::describeInContext

ImageEffect*
ColorCorrectPluginFactory::createInstance(OfxImageEffectHandle handle,
                                          ContextEnum /*context*/)
{
    const ImageEffectHostDescription &gHostDescription = *getImageEffectHostDescription();
    const bool supportsParametricParameter = ( gHostDescription.supportsParametricParameter &&
                                               !(gHostDescription.hostName == "uk.co.thefoundry.nuke" &&
                                                 8 <= gHostDescription.versionMajor && gHostDescription.versionMajor <= 10) );  // Nuke 8-10 are known to *not* support Parametric

    return new ColorCorrectPlugin(handle, supportsParametricParameter);
}

static ColorCorrectPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
