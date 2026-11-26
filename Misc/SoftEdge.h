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
 * The soft-edge curves of the shape plug-ins (Crop, CopyRectangle, Radial, Rectangle, DiscBlades).
 * TODO: move to SupportExt (next to ofxsRamp, whose Smooth curves are the same).
 */

#ifndef openfx_misc_SoftEdge_h
#define openfx_misc_SoftEdge_h

namespace OFX {
namespace SoftEdge {

/// smoothstep: 0 for t <= 0, 1 for t >= 1, t^2 (3 - 2t) between (a NaN stays NaN).
inline double
smoothstep(double t)
{
    if (t <= 0.) {
        return 0.;
    }
    if (t >= 1.) {
        return 1.;
    }

    return t * t * (3. - 2. * t);
}

/// Two parabolas, 2t^2 for t < 1/2 then 1 - 2(1 - t)^2; not clamped. The softness of the older
/// versions, written as they had it so that their output is unchanged.
inline double
rampSmooth(double t)
{
    t *= 2.;
    if (t < 1.) {
        return t * t / 2.;
    }
    t -= 1.;

    return -0.5 * (t * (t - 2.) - 1.);
}

/// The falloff curve of a soft edge (the options of the Radial and Rectangle Falloff parameter, in order).
enum class FalloffEnum
{
    eFalloffQuadratic = 0, // rampSmooth(); the default before version 3
    eFalloffLinear,
    eFalloffSmooth0,
    eFalloffSmooth1,
    eFalloffSmooth,
};

/// The weight at the distance t across the soft edge (0 at the outer edge, 1 at the inner one).
/// Quadratic is rampSmooth(), not clamped; the other curves are clamped to [0, 1] (NaN gives 0),
/// and Smooth0, Smooth1 and Smooth are those of ofxsRamp (the Ramp plug-in).
inline double
falloff(FalloffEnum curve,
        double t)
{
    if (curve == FalloffEnum::eFalloffQuadratic) {
        return rampSmooth(t);
    }
    if ( !(t > 0.) ) {
        return 0.;
    }
    if (t >= 1.) {
        return 1.;
    }
    switch (curve) {
    case FalloffEnum::eFalloffLinear:

        return t;
    case FalloffEnum::eFalloffSmooth0:

        return t * t * (2. - t);
    case FalloffEnum::eFalloffSmooth1:

        return t * ( 1. + t * (1. - t) );
    case FalloffEnum::eFalloffSmooth:
    default:

        return smoothstep(t);
    }
}

/// The weight of a pixel whose center is at the distance d inside the edge (negative outside), for a
/// soft edge of width softness and a half pixel halfPixel (in the same units): the falloff curve of
/// (d + halfPixel) / (softness + 2 halfPixel), so that the edge is also antialiased over one pixel;
/// 0 outside of the edge (also for a NaN distance) and 1 inside.
inline double
softEdgeWeight(FalloffEnum curve,
               double d,
               double softness,
               double halfPixel)
{
    const double t = (d + halfPixel) / (softness + 2. * halfPixel);

    if ( !(t > 0.) ) {
        return 0.;
    }
    if (t >= 1.) {
        return 1.;
    }

    return falloff(curve, t);
}
} // namespace SoftEdge
} // namespace OFX

#endif // openfx_misc_SoftEdge_h
