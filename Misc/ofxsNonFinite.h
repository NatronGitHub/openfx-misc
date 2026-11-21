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
 * Comparison-only clamps for non-finite values, shared by the keyers (Keyer, ChromaKeyer, PIK,
 * Despill, Difference; issues #195 and #213).
 *
 * NaN rule: no behaviour depends on detecting NaN (there is no isnan() test), every operation that
 * could produce NaN is guarded by checks on its inputs, and every finite value in range is
 * unchanged. These clamps are used on the input samples (so that the computations never see
 * inf * 0 or inf - inf), on sums and products that may overflow to +-inf (never NaN, since their
 * operands are finite), and on double results converted to float (converting a double beyond the
 * float range to float is undefined).
 */

#ifndef openfx_misc_ofxsNonFinite_h
#define openfx_misc_ofxsNonFinite_h

#include <cfloat> // FLT_MAX

namespace OFX {
// A value in the float range: +-inf (or a double beyond the float range) gives +-FLT_MAX, and NaN
// gives 0 (every comparison with NaN is false).
template<typename T>
inline T
clampToFloatRange(T v)
{
    return (v > -FLT_MAX) ? ( (v < FLT_MAX) ? v : T(FLT_MAX) ) : ( (v < 0) ? T(-FLT_MAX) : T(0) );
}

// clamp to [0,1], NaN gives 0
template<typename T>
inline T
clampToUnit(T v)
{
    return (v >= 0) ? ( (v < 1) ? v : T(1) ) : T(0);
}

// A pixel sample copied to the output: a float sample (maxValue == 1) is made finite (see
// clampToFloatRange), an integer sample is unchanged.
template<class PIX, int maxValue>
inline PIX
finiteSample(PIX v)
{
    return (maxValue == 1) ? PIX( clampToFloatRange<float>(v) ) : v;
}
} // namespace OFX

#endif // ifndef openfx_misc_ofxsNonFinite_h
