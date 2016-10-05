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
 * OFX ColorCorrect plugin.
 */

#include <cmath>
#include <algorithm>
#include <cfloat> // DBL_MAX
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsCoords.h"
#include "ofxsMacros.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "ColorCorrectOFX"
#define kPluginGrouping "Color"
#define kPluginDescription "Adjusts the saturation, constrast, gamma, gain and offset of an image.\n" \
    "The ranges of the shadows, midtones and highlights are controlled by the curves " \
"in the \"Ranges\" tab.\n" \
"See also: http://opticalenquiry.com/nuke/index.php?title=ColorCorrect"

#define kPluginIdentifier "net.sf.openfx.ColorCorrectPlugin"
// History:
// version 1.0: initial version
// version 2.0: use kNatronOfxParamProcess* parameters
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

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
#define kParamColorCorrectToneRanges "toneRanges"
#define kParamColorCorrectToneRangesLabel "Tone Ranges"
#define kParamColorCorrectToneRangesHint "Tone ranges lookup table"
#define kParamColorCorrectToneRangesDim0 "Shadow"
#define kParamColorCorrectToneRangesDim1 "Highlight"

#define kParamClampBlack "clampBlack"
#define kParamClampBlackLabel "Clamp Black"
#define kParamClampBlackHint "All colors below 0 on output are set to 0."

#define kParamClampWhite "clampWhite"
#define kParamClampWhiteLabel "Clamp White"
#define kParamClampWhiteHint "All colors above 1 on output are set to 1."

#define kParamPremultChanged "premultChanged"

#define LUT_MAX_PRECISION 100

#pragma message WARN("TODO: luminanceMath option")
// Rec.709 luminance:
//Y = 0.2126 R + 0.7152 G + 0.0722 B
static const double s_rLum = 0.2126;
static const double s_gLum = 0.7152;
static const double s_bLum = 0.0722;
struct ColorControlValues
{
    double r;
    double g;
    double b;
    double a;

    ColorControlValues() : r(0.), g(0.), b(0.), a(0.) {}

    void getValueFrom(double time,
                      OFX::RGBAParam* p)
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

template<bool processR, bool processG, bool processB, bool processA>
struct RGBAPixel
{
    double r, g, b, a;

    RGBAPixel(double r_,
              double g_,
              double b_,
              double a_)
        : r(r_)
        , g(g_)
        , b(b_)
        , a(a_)
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
        double tmp_r, tmp_g, tmp_b;

        if (processR) {
            tmp_r = r * ( (1.f - c.r) * s_rLum + c.r ) + g * ( (1.f - c.r) * s_gLum ) + b * ( (1.f - c.r) * s_bLum );
        }
        if (processG) {
            tmp_g = g * ( (1.f - c.g) * s_gLum + c.g ) + r * ( (1.f - c.g) * s_rLum ) + b * ( (1.f - c.g) * s_bLum );
        }
        if (processB) {
            tmp_b = b * ( (1.f - c.b) * s_bLum + c.b ) + g * ( (1.f - c.b) * s_gLum ) + r * ( (1.f - c.b) * s_rLum );
        }
        if (processR) {
            r = tmp_r;
        }
        if (processG) {
            g = tmp_g;
        }
        if (processB) {
            b = tmp_b;
        }
    }

    void applyContrast(const ColorControlValues &c)
    {
        if (processR) {
            r = (r - 0.5f) * c.r  + 0.5f;
        }
        if (processG) {
            g = (g - 0.5f) * c.g  + 0.5f;
        }
        if (processB) {
            b = (b - 0.5f) * c.b  + 0.5f;
        }
        if (processA) {
            a = (a - 0.5f) * c.a  + 0.5f;
        }
    }

    void applyGain(const ColorControlValues &c)
    {
        if (processR) {
            r = r * c.r;
        }
        if (processG) {
            g = g * c.g;
        }
        if (processB) {
            b = b * c.b;
        }
        if (processA) {
            a = a * c.a;
        }
    }

    void applyGamma(const ColorControlValues &c)
    {
        if ( processR && (r > 0) ) {
            r = std::pow(r, 1. / c.r);
        }
        if ( processG && (g > 0) ) {
            g = std::pow(g, 1. / c.g);
        }
        if ( processB && (b > 0) ) {
            b = std::pow(b, 1. / c.b);
        }
        if ( processA && (a > 0) ) {
            a = std::pow(a, 1. / c.a);
        }
    }

    void applyOffset(const ColorControlValues &c)
    {
        if (processR) {
            r = r + c.r;
        }
        if (processG) {
            g = g + c.g;
        }
        if (processB) {
            b = b + c.b;
        }
        if (processA) {
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
    : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImg;
    const OFX::Image *_maskImg;
    bool _premult;
    int _premultChannel;
    bool _doMasking;
    double _mix;
    bool _maskInvert;
    bool _processR, _processG, _processB, _processA;

public:
    ColorCorrecterBase(OFX::ImageEffect &instance,
                       const OFX::RenderArguments & /*args*/)
        : OFX::ImageProcessor(instance)
        , _srcImg(0)
        , _maskImg(0)
        , _premult(false)
        , _premultChannel(3)
        , _doMasking(false)
        , _mix(1.)
        , _maskInvert(false)
        , _processR(false)
        , _processG(false)
        , _processB(false)
        , _processA(false)
        , _clampBlack(true)
        , _clampWhite(true)
    {
    }

    void setSrcImg(const OFX::Image *v) {_srcImg = v; }

    void setMaskImg(const OFX::Image *v,
                    bool maskInvert) {_maskImg = v; _maskInvert = maskInvert; }

    void doMasking(bool v) {_doMasking = v; }

    void setColorControlValues(const ColorControlGroup& master,
                               const ColorControlGroup& shadow,
                               const ColorControlGroup& midtone,
                               const ColorControlGroup& hightlights,
                               bool clampBlack,
                               bool clampWhite,
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
        _clampBlack = clampBlack;
        _clampWhite = clampWhite;
        _premult = premult;
        _premultChannel = premultChannel;
        _mix = mix;
        _processR = processR;
        _processG = processG;
        _processB = processB;
        _processA = processA;
    }

    template<bool processR, bool processG, bool processB, bool processA>
    void colorTransform(double *r,
                        double *g,
                        double *b,
                        double *a)
    {
        double luminance = *r * s_rLum + *g * s_gLum + *b * s_bLum;
        double s_scale = interpolate(0, luminance);
        double h_scale = interpolate(1, luminance);
        double m_scale = 1.f - s_scale - h_scale;

        RGBAPixel<processR, processG, processB, processA> p(*r, *g, *b, *a);
        p.applySMH(_shadowValues, s_scale,
                   _midtoneValues, m_scale,
                   _highlightsValues, h_scale,
                   _masterValues);
        if (processR) {
            *r = clamp(p.r);
        }
        if (processG) {
            *g = clamp(p.g);
        }
        if (processB) {
            *b = clamp(p.b);
        }
        if (processA) {
            *a = clamp(p.a);
        }
    }

private:
    double clamp(double comp)
    {
        if ( _clampBlack && (comp < 0.) ) {
            comp = 0.;
        } else if ( _clampWhite && (comp > 1.0) ) {
            comp = 1.0;
        }

        return comp;
    }

    double interpolate(int curve,
                       double value)
    {
        if (value < 0.) {
            return _lookupTable[curve][0];
        } else if (value >= 1.) {
            return _lookupTable[curve][LUT_MAX_PRECISION];
        } else {
            double i_d = std::floor(value * LUT_MAX_PRECISION);
            int i = (int)i_d;
            assert(i < LUT_MAX_PRECISION);
            double alpha = value * LUT_MAX_PRECISION - i_d;
            assert(0. <= alpha && alpha < 1.);

            return _lookupTable[curve][i] * (1. - alpha) + _lookupTable[curve][i] * alpha;
        }
    }

private:
    ColorControlGroup _masterValues;
    ColorControlGroup _shadowValues;
    ColorControlGroup _midtoneValues;
    ColorControlGroup _highlightsValues;
    bool _clampBlack;
    bool _clampWhite;

protected:

    // clamp for integer types
    template<class PIX>
    double clamp(double value,
                 int maxValue)
    {
        return std::max( 0., std::min( value, double(maxValue) ) );
    }

    double _lookupTable[2][LUT_MAX_PRECISION + 1];
};


template <class PIX, int nComponents, int maxValue>
class ColorCorrecter
    : public ColorCorrecterBase
{
public:
    ColorCorrecter(OFX::ImageEffect &instance,
                   const OFX::RenderArguments &args,
                   bool supportsParametricParameter)
        : ColorCorrecterBase(instance, args)
    {
        const double time = args.time;
        // build the LUT
        OFX::ParametricParam  *lookupTable = 0;

        if (supportsParametricParameter) {
            lookupTable = instance.fetchParametricParam(kParamColorCorrectToneRanges);
        }
        for (int curve = 0; curve < 2; ++curve) {
            for (int position = 0; position <= LUT_MAX_PRECISION; ++position) {
                // position to evaluate the param at
                double parametricPos = double(position) / LUT_MAX_PRECISION;

                // evaluate the parametric param
                double value;
                if (lookupTable) {
                    value = lookupTable->getValue(curve, time, parametricPos);
                } else if (curve == 0) {
                    if (parametricPos < 0.09) {
                        value = 1. - parametricPos / 0.09;
                    } else {
                        value = 0.;
                    }
                } else {
                    assert(curve == 1);
                    if (parametricPos <= 0.5) {
                        value = 0.;
                    } else {
                        value = (parametricPos - 0.5) / 0.5;
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
    }
};

struct ColorControlParamGroup
{
    ColorControlParamGroup()
        : enable(0)
        , saturation(0)
        , contrast(0)
        , gamma(0)
        , gain(0)
        , offset(0) {}

    OFX::BooleanParam* enable;
    OFX::RGBAParam* saturation;
    OFX::RGBAParam* contrast;
    OFX::RGBAParam* gamma;
    OFX::RGBAParam* gain;
    OFX::RGBAParam* offset;
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ColorCorrectPlugin
    : public OFX::ImageEffect
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
        , _dstClip(0)
        , _srcClip(0)
        , _maskClip(0)
        , _processR(0)
        , _processG(0)
        , _processB(0)
        , _processA(0)
        , _rangesParam(0)
        , _clampBlack(0)
        , _clampWhite(0)
        , _premult(0)
        , _premultChannel(0)
        , _mix(0)
        , _maskApply(0)
        , _maskInvert(0)
        , _premultChanged(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == OFX::eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA) ) );
        _maskClip = fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || !_maskClip->isConnected() || _maskClip->getPixelComponents() == ePixelComponentAlpha);
        fetchColorControlGroup(kGroupMaster, &_masterParamsGroup);
        fetchColorControlGroup(kGroupShadows, &_shadowsParamsGroup);
        fetchColorControlGroup(kGroupMidtones, &_midtonesParamsGroup);
        fetchColorControlGroup(kGroupHighlights, &_highlightsParamsGroup);
        if (_supportsParametricParameter) {
            _rangesParam = fetchParametricParam(kParamColorCorrectToneRanges);
            assert(_rangesParam);
        }
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

        _processR = fetchBooleanParam(kNatronOfxParamProcessR);
        _processG = fetchBooleanParam(kNatronOfxParamProcessG);
        _processB = fetchBooleanParam(kNatronOfxParamProcessB);
        _processA = fetchBooleanParam(kNatronOfxParamProcessA);
        assert(_processR && _processG && _processB && _processA);
    }

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(ColorCorrecterBase &, const OFX::RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;
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
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;
    OFX::Clip *_maskClip;
    ColorControlParamGroup _masterParamsGroup;
    ColorControlParamGroup _shadowsParamsGroup;
    ColorControlParamGroup _midtonesParamsGroup;
    ColorControlParamGroup _highlightsParamsGroup;
    BooleanParam* _processR;
    BooleanParam* _processG;
    BooleanParam* _processB;
    BooleanParam* _processA;
    OFX::ParametricParam* _rangesParam;
    OFX::BooleanParam* _clampBlack;
    OFX::BooleanParam* _clampWhite;
    OFX::BooleanParam* _premult;
    OFX::ChoiceParam* _premultChannel;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskApply;
    OFX::BooleanParam* _maskInvert;
    OFX::BooleanParam* _premultChanged; // set to true the first time the user connects src
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
                                    const OFX::RenderArguments &args)
{
    const double time = args.time;
    std::auto_ptr<OFX::Image> dst( _dstClip->fetchImage(args.time) );

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
                                         _srcClip->fetchImage(args.time) : 0 );
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
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(args.time) ) && _maskClip && _maskClip->isConnected() );
    std::auto_ptr<const OFX::Image> mask(doMasking ? _maskClip->fetchImage(args.time) : 0);
    if ( mask.get() ) {
        if ( (mask->getRenderScale().x != args.renderScale.x) ||
             ( mask->getRenderScale().y != args.renderScale.y) ||
             ( ( mask->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( mask->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );
    processor.setRenderWindow(args.renderWindow);

    ColorControlGroup masterValues, shadowValues, midtoneValues, highlightValues;
    getColorCorrectGroupValues(args.time, &masterValues,    eGroupMaster);
    getColorCorrectGroupValues(args.time, &shadowValues,    eGroupShadow);
    getColorCorrectGroupValues(args.time, &midtoneValues,   eGroupMidtone);
    getColorCorrectGroupValues(args.time, &highlightValues, eGroupHighlight);
    bool clampBlack, clampWhite;
    _clampBlack->getValueAtTime(args.time, clampBlack);
    _clampWhite->getValueAtTime(args.time, clampWhite);
    bool premult;
    int premultChannel;
    _premult->getValueAtTime(args.time, premult);
    _premultChannel->getValueAtTime(args.time, premultChannel);
    double mix;
    _mix->getValueAtTime(args.time, mix);

    bool processR, processG, processB, processA;
    _processR->getValueAtTime(time, processR);
    _processG->getValueAtTime(time, processG);
    _processB->getValueAtTime(time, processB);
    _processA->getValueAtTime(time, processA);

    processor.setColorControlValues(masterValues, shadowValues, midtoneValues, highlightValues, clampBlack, clampWhite, premult, premultChannel, mix,
                                    processR, processG, processB, processA);
    processor.process();
} // ColorCorrectPlugin::setupAndProcess

// the overridden render function
void
ColorCorrectPlugin::render(const OFX::RenderArguments &args)
{
    //std::cout << "render!\n";
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA);
    if (dstComponents == OFX::ePixelComponentRGBA) {
        switch (dstBitDepth) {
        case OFX::eBitDepthUByte: {
            ColorCorrecter<unsigned char, 4, 255> fred(*this, args, _supportsParametricParameter);
            setupAndProcess(fred, args);
            break;
        }
        case OFX::eBitDepthUShort: {
            ColorCorrecter<unsigned short, 4, 65535> fred(*this, args, _supportsParametricParameter);
            setupAndProcess(fred, args);
            break;
        }
        case OFX::eBitDepthFloat: {
            ColorCorrecter<float, 4, 1> fred(*this, args, _supportsParametricParameter);
            setupAndProcess(fred, args);
            break;
        }
        default:
            //std::cout << "depth usupported\n";
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else {
        assert(dstComponents == OFX::ePixelComponentRGB);
        switch (dstBitDepth) {
        case OFX::eBitDepthUByte: {
            ColorCorrecter<unsigned char, 3, 255> fred(*this, args, _supportsParametricParameter);
            setupAndProcess(fred, args);
            break;
        }
        case OFX::eBitDepthUShort: {
            ColorCorrecter<unsigned short, 3, 65535> fred(*this, args, _supportsParametricParameter);
            setupAndProcess(fred, args);
            break;
        }
        case OFX::eBitDepthFloat: {
            ColorCorrecter<float, 3, 1> fred(*this, args, _supportsParametricParameter);
            setupAndProcess(fred, args);
            break;
        }
        default:
            //std::cout << "components usupported\n";
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
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
                               double & /*identityTime*/)
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
            if (OFX::getImageEffectHostDescription()->supportsMultiResolution) {
                // In Sony Catalyst Edit, clipGetRegionOfDefinition returns the RoD in pixels instead of canonical coordinates.
                // In hosts that do not support multiResolution (e.g. Sony Catalyst Edit), all inputs have the same RoD anyway.
                OFX::Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(args.time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
                // effect is identity if the renderWindow doesn't intersect the mask RoD
                if ( !OFX::Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0) ) {
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
    //std::cout << "changedClip OK!\n";
}

void
ColorCorrectPlugin::changedParam(const OFX::InstanceChangedArgs &args,
                                 const std::string &paramName)
{
    if ( (paramName == kParamPremult) && (args.reason == OFX::eChangeUserEdit) ) {
        _premultChanged->setValue(true);
    }
}

mDeclarePluginFactory(ColorCorrectPluginFactory, {}, {});
void
ColorCorrectPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
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
    desc.setChannelSelector(OFX::ePixelComponentNone); // we have our own channel selector
#endif
}

static void
defineRGBAScaleParam(OFX::ImageEffectDescriptor &desc,
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
                 OFX::ImageEffectDescriptor &desc,
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
ColorCorrectPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                             OFX::ContextEnum context)
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
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessR);
        param->setLabel(kParamProcessRLabel);
        param->setHint(kParamProcessRHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessG);
        param->setLabel(kParamProcessGLabel);
        param->setHint(kParamProcessGHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessB);
        param->setLabel(kParamProcessBLabel);
        param->setHint(kParamProcessBHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessA);
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

    PageParamDescriptor* ranges = desc.definePageParam("Ranges");
    const ImageEffectHostDescription &gHostDescription = *OFX::getImageEffectHostDescription();
    const bool supportsParametricParameter = ( gHostDescription.supportsParametricParameter &&
                                               !(gHostDescription.hostName == "uk.co.thefoundry.nuke" &&
                                                 8 <= gHostDescription.versionMajor && gHostDescription.versionMajor <= 10) );  // Nuke 8-10 are known to *not* support Parametric
    if (supportsParametricParameter) {
        OFX::ParametricParamDescriptor* param = desc.defineParametricParam(kParamColorCorrectToneRanges);
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

        param->addControlPoint(0, // curve to set
                               0.0,         // time, ignored in this case, as we are not adding a key
                               0.0,         // parametric position, zero
                               1.0,         // value to be, 0
                               false);         // don't add a key
        param->addControlPoint(0, 0.0, 0.09, 0.0, false);

        param->addControlPoint(1, 0.0, 0.5, 0.0, false);
        param->addControlPoint(1, 0.0, 1.0, 1.0, false);
        ranges->addChild(*param);
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
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamPremultChanged);
        param->setDefault(false);
        param->setIsSecretAndDisabled(true);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }
} // ColorCorrectPluginFactory::describeInContext

OFX::ImageEffect*
ColorCorrectPluginFactory::createInstance(OfxImageEffectHandle handle,
                                          OFX::ContextEnum /*context*/)
{
    const ImageEffectHostDescription &gHostDescription = *OFX::getImageEffectHostDescription();
    const bool supportsParametricParameter = ( gHostDescription.supportsParametricParameter &&
                                               !(gHostDescription.hostName == "uk.co.thefoundry.nuke" &&
                                                 8 <= gHostDescription.versionMajor && gHostDescription.versionMajor <= 10) );  // Nuke 8-10 are known to *not* support Parametric

    return new ColorCorrectPlugin(handle, supportsParametricParameter);
}

static ColorCorrectPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
