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
 * The pixel input code shared by the Keyer, ChromaKeyer and PIK plug-ins.
 */

#ifndef openfx_misc_KeyerUtils_h
#define openfx_misc_KeyerUtils_h

#include <algorithm>

#include "ofxsNonFinite.h" // clampToUnit, sampleToFloat, finiteSampleToFloat

namespace OFX {
namespace KeyerUtils {

using OFX::sampleToFloat;
using OFX::finiteSampleToFloat;

/// The inside and outside masks of a keyer at a pixel, in [0,1].
struct Masks
{
    float inside;
    float outside;
};

/// The least inside mask that the Source pixel gives with Add to Inside Mask (pass srcPix nullptr
/// otherwise: 0, nothing is added): its alpha, made finite (see finiteSampleToFloat()), or 1 for an RGB
/// Source (srcHasAlpha false), which is opaque as when the host maps it to RGBA (srcPix[3] would be the
/// next pixel's red).
template<class PIX, int maxValue>
inline float
insideMaskFromSource(const PIX* srcPix,
                     bool srcHasAlpha)
{
    if (!srcPix) {
        return 0.f;
    }

    return srcHasAlpha ? finiteSampleToFloat<PIX, maxValue>(srcPix[3]) : 1.f;
}

/// The masks at a pixel from the mask samples (0 if there is no mask), clamped to [0,1] (NaN gives 0).
/// The inside mask is at least insideAtLeast (before the clamp; see insideMaskFromSource()): 0 changes
/// nothing, since the clamp gives 0 for a NaN or a negative mask anyway.
template<class PIX, int maxValue>
inline Masks
masks(const PIX* inMaskPix,
      const PIX* outMaskPix,
      float insideAtLeast)
{
    const float inMask = (std::max)(inMaskPix ? sampleToFloat<PIX, maxValue>(*inMaskPix) : 0.f, insideAtLeast);
    const float outMask = outMaskPix ? sampleToFloat<PIX, maxValue>(*outMaskPix) : 0.f;

    return Masks{ clampToUnit(inMask), clampToUnit(outMask) };
}
} // namespace KeyerUtils
} // namespace OFX

#endif // openfx_misc_KeyerUtils_h
