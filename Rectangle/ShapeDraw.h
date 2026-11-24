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
 * The Draw options shared by the Radial and Rectangle plugins: Premultiply, Replace and Invert.
 * Included by Rectangle/Rectangle.cpp and Radial/Radial.cpp.
 */

#ifndef openfx_misc_ShapeDraw_h
#define openfx_misc_ShapeDraw_h

#include <algorithm>
#include <cassert>
#include <cfloat>

#include "ofxsImageEffect.h"

#define kParamShapePremult "premultiply"
#define kParamShapePremultLabel "Premultiply"
#define kParamShapePremultHint "Multiply the red, green and blue channels of the Source by the shape (1 inside, 0 outside, " \
    "and the Softness in between; inverted by Invert) before drawing: outside of the shape, they are set to black. " \
    "With Replace and A checked, Color 0 alpha 0 and Color 1 alpha 1, the output alpha is the shape and the output is premultiplied."

#define kParamShapeReplace "replace"
#define kParamShapeReplaceLabel "Replace"
#define kParamShapeReplaceHint "Clear the drawn channels (R, G, B and A, as checked) of the Source to black before drawing into them."

#define kParamShapeInvert "invert"
#define kParamShapeInvertLabel "Invert"
#define kParamShapeInvertHint "Invert the shape: Color 0 is drawn inside and Color 1 outside, and Premultiply uses the inverted shape."

namespace OFX {
namespace ShapeDraw {

/// The drawing options at one time. All off by default: the output of older versions.
struct Options
{
    bool premult = false; // multiply the Source RGB by the shape weight
    bool replace = false; // clear the drawn channels of the Source
    bool invert = false; // swap the colors, and invert the Premultiply weight
};

/// The Source pixel srcPixRGBA (RGBA, not normalized), before the shape is drawn over it: Premultiply
/// multiplies its RGB by the shape weight w (1 inside the shape, 0 outside; 1 - w with Invert), and
/// Replace clears the processed channels. A zero weight gives 0, also for an infinite Source
/// (inf * 0 would be NaN), and an infinite Source is made finite (+-FLT_MAX) first.
template<bool processR, bool processG, bool processB, bool processA>
inline void
prepareSource(const Options& options,
              float w,
              float srcPixRGBA[4])
{
    if (options.premult) {
        const float m = options.invert ? 1.f - w : w;
        for (int c = 0; c < 3; ++c) {
            srcPixRGBA[c] = (m == 0.f) ? 0.f : (std::max)( -FLT_MAX, (std::min)(srcPixRGBA[c], FLT_MAX) ) * m;
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
    {
        assert(_color0 && _color1 && _premult && _replace && _invert);
    }

    Options options(double time) const
    {
        Options o;
        o.premult = _premult->getValueAtTime(time);
        o.replace = _replace->getValueAtTime(time);
        o.invert = _invert->getValueAtTime(time);

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

    /// true if the Output alpha is the shape and the RGB are premultiplied by it: Premultiply, Replace
    /// and Process A on an RGBA Source, with Color 0 alpha 0 and Color 1 alpha 1 (the alpha is then
    /// the shape weight w, or 1 - w with Invert, which is the Premultiply weight). For the clip preferences.
    bool outputIsPremultipliedByShape(bool processA,
                                      const Clip* srcClip) const
    {
        if ( !processA || !_premult->getValue() || !_replace->getValue() ||
             !srcClip || !srcClip->isConnected() || (srcClip->getPixelComponents() != ePixelComponentRGBA) ) {
            return false;
        }
        double r, g, b, a0, a1;
        _color0->getValue(r, g, b, a0);
        _color1->getValue(r, g, b, a1);

        return (a0 == 0.) && (a1 == 1.);
    }

private:
    RGBAParam* _color0;
    RGBAParam* _color1;
    BooleanParam* _premult;
    BooleanParam* _replace;
    BooleanParam* _invert;
};

/// Describe the parameters, at the end of the page: new parameters keep the output of older versions.
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
}
} // namespace ShapeDraw
} // namespace OFX

#endif // openfx_misc_ShapeDraw_h
