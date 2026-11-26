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
 * The Draw options shared by the Radial and Rectangle plugins: Premultiply, Replace and Invert,
 * and the falloff curve of the Softness, and the pixel writing code of their processors.
 * Included by Rectangle/Rectangle.cpp and Radial/Radial.cpp.
 */

#ifndef openfx_misc_ShapeDraw_h
#define openfx_misc_ShapeDraw_h

#include <algorithm>
#include <cassert>
#include <cfloat>

#include "ofxsImageEffect.h"
#include "ofxsProcessing.H"
#include "ofxsMaskMix.h" // kParamMix, kParamMaskApply, ofxsMaskMixPix
#include "ofxsMerging.h" // MergeImages2D::mergeFiniteInput
#ifdef OFX_EXTENSIONS_NATRON
#include "ofxNatron.h"
#endif
#include "SoftEdge.h"

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
#define kParamProcessRHint  "Process red channel."
#define kParamProcessG      "processG"
#define kParamProcessGLabel "G"
#define kParamProcessGHint  "Process green channel."
#define kParamProcessB      "processB"
#define kParamProcessBLabel "B"
#define kParamProcessBHint  "Process blue channel."
#define kParamProcessA      "processA"
#define kParamProcessALabel "A"
#define kParamProcessAHint  "Process alpha channel."
#endif

#define kParamShapePremult "premultiply"
#define kParamShapePremultLabel "Premultiply"
#define kParamShapePremultHint "Multiply the red, green and blue channels of the Source by the shape (1 inside, 0 outside, " \
    "and the Softness in between; inverted by Invert) before drawing: outside of the shape, they are set to black. " \
    "With Replace and A checked, Color 0 black and transparent (in the drawn channels) and Color 1 alpha 1, the output alpha is the shape and the output is premultiplied " \
    "(also with Mix below 1 or a Mask, if the Source is premultiplied or opaque). Does nothing on an Alpha Source."

#define kParamShapeReplace "replace"
#define kParamShapeReplaceLabel "Replace"
#define kParamShapeReplaceHint "Clear the drawn channels (R, G, B and A, as checked) of the Source to black before drawing into them."

#define kParamShapeInvert "invert"
#define kParamShapeInvertLabel "Invert"
#define kParamShapeInvertHint "Invert the shape: Color 0 is drawn inside and Color 1 outside, and Premultiply uses the inverted shape."

#define kParamShapeFalloff "falloff"
#define kParamShapeFalloffLabel "Falloff"
#define kParamShapeFalloffHint "The curve of the edge in the Softness, from Color 0 (0) to Color 1 (1), as a function of " \
    "the distance t across the Softness, from 0 at the outer edge to 1 at the inner edge."
#define kParamShapeFalloffOptionQuadratic "Quadratic", "Two parabolas, 2t^2 then 1 - 2(1 - t)^2 (the default before version 3).", "quadratic"
#define kParamShapeFalloffOptionLinear "Linear", "t.", "linear"
#define kParamShapeFalloffOptionSmooth0 "Smooth0", "Smooth at the outer edge, linear at the inner edge: t^2 (2 - t).", "smooth0"
#define kParamShapeFalloffOptionSmooth1 "Smooth1", "Linear at the outer edge, smooth at the inner edge: t (1 + t (1 - t)).", "smooth1"
#define kParamShapeFalloffOptionSmooth "Smooth", "Smooth at both edges (smoothstep): t^2 (3 - 2t). The default since version 3.", "smooth"

namespace OFX {
namespace ShapeDraw {

/// The falloff curve of the Softness (the options of the Falloff parameter, in order).
using SoftEdge::FalloffEnum;
using SoftEdge::falloff;

/// Describe the Falloff parameter (put it after Softness), with its default.
inline void
describeFalloffParam(ImageEffectDescriptor& desc,
                     PageParamDescriptor* page,
                     FalloffEnum defaultCurve)
{
    ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamShapeFalloff);
    param->setLabel(kParamShapeFalloffLabel);
    param->setHint(kParamShapeFalloffHint);
    assert(param->getNOptions() == (int)FalloffEnum::eFalloffQuadratic);
    param->appendOption(kParamShapeFalloffOptionQuadratic);
    assert(param->getNOptions() == (int)FalloffEnum::eFalloffLinear);
    param->appendOption(kParamShapeFalloffOptionLinear);
    assert(param->getNOptions() == (int)FalloffEnum::eFalloffSmooth0);
    param->appendOption(kParamShapeFalloffOptionSmooth0);
    assert(param->getNOptions() == (int)FalloffEnum::eFalloffSmooth1);
    param->appendOption(kParamShapeFalloffOptionSmooth1);
    assert(param->getNOptions() == (int)FalloffEnum::eFalloffSmooth);
    param->appendOption(kParamShapeFalloffOptionSmooth);
    param->setDefault( static_cast<int>(defaultCurve) );
    if (page) {
        page->addChild(*param);
    }
}

/// The drawing options at one time. All off by default: the output of older versions.
struct Options
{
    bool premult = false; // multiply the Source RGB by the shape weight
    bool replace = false; // clear the drawn channels of the Source
    bool invert = false; // swap the colors, and invert the Premultiply weight
    FalloffEnum falloff = FalloffEnum::eFalloffQuadratic; // the curve of the Softness
};

/// The shape weight of a pixel in the Softness, in versions 3 and later (ProcessorBase::_pixelCenterSoftness):
/// SoftEdge::softEdgeWeight() at the pixel center, whose distance inside the edge is d, for a Softness and a
/// half pixel in the same units (halfPixel 0: no other antialiasing, the Softness is the edge).
inline float
softWeight(FalloffEnum curve,
           double d,
           double softness,
           double halfPixel,
           bool plinear)
{
    double w = SoftEdge::softEdgeWeight(curve, d, softness, halfPixel);
    if (plinear) {
        w = w * w * w; // perceptually linear
    }

    return static_cast<float>(w);
}

/// The Source pixel srcPixRGBA (RGBA, not normalized), before the shape is drawn over it: Premultiply
/// multiplies its RGB by the shape weight w (1 inside the shape, 0 outside; 1 - w with Invert), and
/// Replace clears the processed channels. A zero weight gives 0, also for an infinite or NaN Source
/// (inf * 0 would be NaN), and an infinite Source is made finite (+-FLT_MAX) first, as in the Merge
/// operators (MergeImages2D::mergeFiniteInput; a NaN stays NaN where the weight is not 0).
template<bool processR, bool processG, bool processB, bool processA>
inline void
prepareSource(const Options& options,
              float w,
              float srcPixRGBA[4])
{
    if (options.premult) {
        const float m = options.invert ? 1.f - w : w;
        for (int c = 0; c < 3; ++c) {
            srcPixRGBA[c] = (m == 0.f) ? 0.f : MergeImages2D::mergeFiniteInput<float>(srcPixRGBA[c]) * m;
        }
    }
    if (options.replace) {
        if (processR) {
            srcPixRGBA[0] = 0.f;
        }
        if (processG) {
            srcPixRGBA[1] = 0.f;
        }
        if (processB) {
            srcPixRGBA[2] = 0.f;
        }
        if (processA) {
            srcPixRGBA[3] = 0.f;
        }
    }
}

/// A color parameter value.
struct RGBAValues
{
    double r, g, b, a;
    RGBAValues(double v) : r(v), g(v), b(v), a(v) {}

    RGBAValues() : r(0), g(0), b(0), a(0) {}
};

/// The members of the Radial and Rectangle processors that do not depend on the shape, and the code that
/// writes a pixel from the shape color. The derived classes set the protected members in their setValues().
class ProcessorBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg;
    const Image *_maskImg;
    bool _doMasking;
    double _mix;
    bool _maskInvert;
    bool _processR;
    bool _processG;
    bool _processB;
    bool _processA;
    RGBAValues _color0, _color1;
    Options _drawOptions; // Premultiply, Replace, Invert, Falloff
    bool _pixelCenterSoftness; // version 3: the Softness is computed at the pixel center (see softWeight())

public:
    ProcessorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _srcImg(NULL)
        , _maskImg(NULL)
        , _doMasking(false)
        , _mix(1.)
        , _maskInvert(false)
        , _processR(false)
        , _processG(false)
        , _processB(false)
        , _processA(false)
        , _color0(0.)
        , _color1(0.)
        , _drawOptions()
        , _pixelCenterSoftness(false)
    {
    }

    /** @brief set the src image */
    void setSrcImg(const Image *v)
    {
        _srcImg = v;
    }

    void setMaskImg(const Image *v,
                    bool maskInvert)
    {
        _maskImg = v;
        _maskInvert = maskInvert;
    }

    void doMasking(bool v)
    {
        _doMasking = v;
    }

    /** @brief version 3 and later: the Softness is computed at the pixel center */
    void setPixelCenterSoftness(bool v)
    {
        _pixelCenterSoftness = v;
    }

protected:
    // tmpPix (RGBA and the shape weight) for the shape weight w: Color 0 (w = 0) to Color 1 (w = 1)
    void setSoftPixel(float w,
                      float tmpPix[5]) const
    {
        tmpPix[0] = (float)_color0.r * (1.f - w) + (float)_color1.r * w;
        tmpPix[1] = (float)_color0.g * (1.f - w) + (float)_color1.g * w;
        tmpPix[2] = (float)_color0.b * (1.f - w) + (float)_color1.b * w;
        tmpPix[3] = (float)_color0.a * (1.f - w) + (float)_color1.a * w;
        tmpPix[4] = w;
    }

    // Composite the shape color tmpPix (normalized, premultiplied) over the source pixel in the
    // processed channels, then apply the mask and mix and write dstPix.
    // Shared by the process functions, which only differ in how they compute tmpPix.
    template<class PIX, int nComponents, int maxValue, bool processR, bool processG, bool processB, bool processA>
    void writePixel(float tmpPix[5],
                    const PIX* srcPix,
                    int x,
                    int y,
                    PIX* dstPix)
    {
        float a = tmpPix[3];

        // ofxsMaskMixPix takes non-normalized values
        tmpPix[0] *= maxValue;
        tmpPix[1] *= maxValue;
        tmpPix[2] *= maxValue;
        tmpPix[3] *= maxValue;
        float srcPixRGBA[4] = {0, 0, 0, 0};
        if (srcPix) {
            if (nComponents >= 3) {
                srcPixRGBA[0] = srcPix[0];
                srcPixRGBA[1] = srcPix[1];
                srcPixRGBA[2] = srcPix[2];
            }
            if ( (nComponents == 1) || (nComponents == 4) ) {
                srcPixRGBA[3] = srcPix[nComponents - 1];
            }
        }
        // Premultiply and Replace; tmpPix[4] is the shape weight
        prepareSource<processR, processG, processB, processA>(_drawOptions, tmpPix[4], srcPixRGBA);
        tmpPix[0] = processR ? (tmpPix[0] + srcPixRGBA[0] * (1.f - a)) : srcPixRGBA[0];
        tmpPix[1] = processG ? (tmpPix[1] + srcPixRGBA[1] * (1.f - a)) : srcPixRGBA[1];
        tmpPix[2] = processB ? (tmpPix[2] + srcPixRGBA[2] * (1.f - a)) : srcPixRGBA[2];
        tmpPix[3] = processA ? (tmpPix[3] + srcPixRGBA[3] * (1.f - a)) : srcPixRGBA[3];
        if (nComponents == 1) {
            // ofxsMaskMixPix reads the only channel from tmpPix[0]
            tmpPix[0] = tmpPix[3];
        }
        ofxsMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
    }
};

/// The colors outside (color0) and inside (color1) of the shape, swapped by Invert.
template<class RGBA>
struct Colors
{
    RGBA color0;
    RGBA color1;
};

/// The parameters of the Draw options, and the colors they apply to.
class Params
{
public:
    Params(ImageEffect& effect,
           const char* color0Name,
           const char* color1Name)
        : _color0( effect.fetchRGBAParam(color0Name) )
        , _color1( effect.fetchRGBAParam(color1Name) )
        , _premult( effect.fetchBooleanParam(kParamShapePremult) )
        , _replace( effect.fetchBooleanParam(kParamShapeReplace) )
        , _invert( effect.fetchBooleanParam(kParamShapeInvert) )
        , _falloff( effect.fetchChoiceParam(kParamShapeFalloff) )
        , _processR( effect.fetchBooleanParam(kParamProcessR) )
        , _processG( effect.fetchBooleanParam(kParamProcessG) )
        , _processB( effect.fetchBooleanParam(kParamProcessB) )
        , _processA( effect.fetchBooleanParam(kParamProcessA) )
        , _mix( effect.fetchDoubleParam(kParamMix) )
        , _maskApply( ( ofxsMaskIsAlwaysConnected( OFX::getImageEffectHostDescription() ) && effect.paramExists(kParamMaskApply) ) ?
                      effect.fetchBooleanParam(kParamMaskApply) : NULL )
    {
        assert(_color0 && _color1 && _premult && _replace && _invert && _falloff);
        assert(_processR && _processG && _processB && _processA && _mix);
    }

    Options options(double time) const
    {
        Options o;
        o.premult = _premult->getValueAtTime(time);
        o.replace = _replace->getValueAtTime(time);
        o.invert = _invert->getValueAtTime(time);
        const int curve = _falloff->getValueAtTime(time);
        // the only range check of the curve (falloff() does not check it)
        o.falloff = ( (curve >= static_cast<int>(FalloffEnum::eFalloffQuadratic)) && (curve <= static_cast<int>(FalloffEnum::eFalloffSmooth)) ) ?
                    static_cast<FalloffEnum>(curve) : FalloffEnum::eFalloffQuadratic;

        return o;
    }

    /// The colors at time, swapped by Invert. RGBA has members r, g, b and a (double).
    template<class RGBA>
    Colors<RGBA> colors(double time) const
    {
        Colors<RGBA> c;
        _color0->getValueAtTime(time, c.color0.r, c.color0.g, c.color0.b, c.color0.a);
        _color1->getValueAtTime(time, c.color1.r, c.color1.g, c.color1.b, c.color1.a);
        if ( _invert->getValueAtTime(time) ) {
            std::swap(c.color0, c.color1);
        }

        return c;
    }

    /// true if Premultiply (on an RGB or RGBA Source) or Replace change the Source also where
    /// nothing is drawn, so that the effect is never a pass-through
    bool changesSource(double time,
                       const Clip* srcClip) const
    {
        if ( _replace->getValueAtTime(time) ) {
            return true;
        }
        if ( !srcClip || !srcClip->isConnected() ) {
            return false;
        }
        const PixelComponentEnum comps = srcClip->getPixelComponents();

        return ( (comps == ePixelComponentRGB) || (comps == ePixelComponentRGBA) ) && _premult->getValueAtTime(time);
    }

    /// The checkboxes of the processed channels.
    struct ProcessChannels
    {
        bool r;
        bool g;
        bool b;
        bool a;
    };

    /// The R, G, B and A checkboxes, at the current time (for the clip preferences).
    ProcessChannels processChannels() const
    {
        return { _processR->getValue(), _processG->getValue(), _processB->getValue(), _processA->getValue() };
    }

    /// true if Mix is 1 and no Mask applies, at the current time (for the clip preferences).
    bool isFullMix(const Clip* maskClip) const
    {
        const bool masked = maskClip && maskClip->isConnected() && ( !_maskApply || _maskApply->getValue() );

        return !masked && (_mix->getValue() == 1.);
    }

    /// outputIsPremultipliedByShape() for the current values of the checkboxes, Mix and the mask.
    bool outputIsPremultipliedByShape(const Clip* srcClip,
                                      const Clip* maskClip) const
    {
        return outputIsPremultipliedByShape( processChannels(), srcClip, isFullMix(maskClip) );
    }

    /// true if the Output alpha is the shape and the RGB are premultiplied by it: Premultiply, Replace
    /// and Process A on an RGB or RGBA Source (the unmapped components: an RGB Source is mapped to RGBA
    /// when A is processed), with Color 0 alpha 0 and Color 1 alpha 1 (the alpha is then the shape
    /// weight w, or 1 - w with Invert, which is the Premultiply weight), and the processed RGB of
    /// Color 0 at 0 (Color 0 is drawn where that alpha is 0: outside, or inside with Invert).
    /// Mix and the Mask mix that with the Source, which keeps it premultiplied only if the Source is
    /// premultiplied or opaque: fullMix is true when Mix is 1 and no Mask applies.
    /// For the clip preferences, which do not depend on time: the colors and Mix are read at the
    /// time of the last clip preferences call (they trigger it), so with animated values the declared
    /// state follows that time.
    bool outputIsPremultipliedByShape(const ProcessChannels& process,
                                      const Clip* srcClip,
                                      bool fullMix) const
    {
        if ( !process.a || !_premult->getValue() || !_replace->getValue() || !srcClip || !srcClip->isConnected() ) {
            return false;
        }
        const PixelComponentEnum srcComps = srcClip->getUnmappedPixelComponents();
        if ( (srcComps != ePixelComponentRGB) && (srcComps != ePixelComponentRGBA) ) {
            return false;
        }
        const PreMultiplicationEnum srcPremult = srcClip->getPreMultiplication();
        if ( !fullMix && (srcPremult != eImagePreMultiplied) && (srcPremult != eImageOpaque) ) {
            return false;
        }
        double r0, g0, b0, a0;
        _color0->getValue(r0, g0, b0, a0);
        double r1, g1, b1, a1;
        _color1->getValue(r1, g1, b1, a1);

        return (a0 == 0.) && (a1 == 1.) &&
               (!process.r || r0 == 0.) && (!process.g || g0 == 0.) && (!process.b || b0 == 0.);
    }

private:
    RGBAParam* _color0;
    RGBAParam* _color1;
    BooleanParam* _premult;
    BooleanParam* _replace;
    BooleanParam* _invert;
    ChoiceParam* _falloff;
    BooleanParam* _processR;
    BooleanParam* _processG;
    BooleanParam* _processB;
    BooleanParam* _processA;
    DoubleParam* _mix;
    BooleanParam* _maskApply; // NULL if the host has no Apply Mask
};

/// Describe the drawing options (Premultiply, Replace, Invert), then the mask and mix parameters
/// (ofxsMaskMixDescribeParams()), in the order of the Radial and Rectangle references: "premult,
/// clip to, replace, invert, opacity, mask, maskChannelInput, inject, invert". Call it right after
/// the output channels. The defaults keep the output of older versions.
inline void
describeParams(ImageEffectDescriptor& desc,
               PageParamDescriptor* page)
{
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamShapePremult);
        param->setLabel(kParamShapePremultLabel);
        param->setHint(kParamShapePremultHint);
        param->setDefault(false);
        param->setAnimates(false); // a clip preferences slave
        desc.addClipPreferencesSlaveParam(*param);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamShapeReplace);
        param->setLabel(kParamShapeReplaceLabel);
        param->setHint(kParamShapeReplaceHint);
        param->setDefault(false);
        param->setAnimates(false); // a clip preferences slave
        desc.addClipPreferencesSlaveParam(*param);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamShapeInvert);
        param->setLabel(kParamShapeInvertLabel);
        param->setHint(kParamShapeInvertHint);
        param->setDefault(false);
        if (page) {
            page->addChild(*param);
        }
    }

    ofxsMaskMixDescribeParams(desc, page);
    // Mix and Apply Mask trigger the clip preferences (see Params::outputIsPremultipliedByShape()).
    for (const char* name : {kParamMix, kParamMaskApply}) {
        if ( ParamDescriptor* param = desc.getParamDescriptor(name) ) {
            desc.addClipPreferencesSlaveParam(*param);
        }
    }
}
} // namespace ShapeDraw
} // namespace OFX

#endif // openfx_misc_ShapeDraw_h
