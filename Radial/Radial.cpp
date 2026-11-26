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
 * OFX Radial plugin.
 */

// NOTE: This plugin is very similar to Rectangle. Any changes made here should probably be made in Rectangle.

#include <cmath>
#include <cfloat> // DBL_MAX
#include <algorithm>

#include "ofxsProcessing.H"
#include "ofxsCoords.h"
#include "ofxsMaskMix.h"
#include "ofxsRGBAProcessing.h"
#include "ofxsMacros.h"
#include "ofxsGenerator.h"
#ifdef OFX_EXTENSIONS_NATRON
#include "ofxNatron.h"
#endif
#include "ofxsThreadSuite.h"
#include "../Rectangle/ShapeDraw.h"

#ifdef __APPLE__
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION // Yes, we are still doing OpenGL 2.1
#endif
#include <OpenGL/gl.h>
#else
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <GL/gl.h>
#endif
#define POINT_TOLERANCE 6
#define POINT_SIZE 5

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "RadialOFX"
#define kPluginGrouping "Draw"
#define kPluginDescription \
    "Radial ramp.\n" \
    "The ramp is composited with the source image using the 'over' operator.\n" \
    "\n" \
    "If no source is connected, this effect behaves like a generator. Its region of definition is:\n" \
    "- The selected format if the Extent parameter is a format.\n" \
    "- The project output format if Color0 is not black and transparent.\n" \
    "- The selected extent (or the bounding box of the rotated ellipse, if Angle is not zero) plus a one-pixel border if Color0 is black and transparent.\n" \
    "\n" \
    "See also: https://web.archive.org/web/20220807182648/http://www.opticalenquiry.com/nuke/index.php?title=Radial"

#define kPluginIdentifier "net.sf.openfx.Radial"
// History:
// version 1.0: initial version
// version 2.0: use kNatronOfxParamProcess* parameters
// version 2.1: antialiased render
// version 2.2: add angle parameter, fix rendering to an Alpha-only output
// version 2.2: not a pass-through when Process A is checked and Source is Opaque,
//              since the Output is then UnPremultiplied (issue #328);
//              add the Premultiply, Replace and Invert parameters (default off: the output is unchanged) (#288);
//              add the Falloff parameter, the curve of the Softness (default Quadratic: the output is unchanged) (#288)
// version 3.0: the Softness is computed at the pixel center (it was the pixel corner nearest to the
//              center), with no other antialiasing, and Falloff is Smooth by default (it was Quadratic): the soft edge
//              matches the tests/nuke reference (#288). Version 2 is kept, obsolete, with its output.
#define kPluginVersionMajor 3 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.
#define kPluginVersionMinorV2 2

// version 3: the Softness at the pixel center, and Falloff Smooth by default
static constexpr unsigned kVersionPixelCenterSoftness = 3;

#define kSupportsByte true
#define kSupportsUShort true
#define kSupportsHalf false
#define kSupportsFloat true

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

// kParamProcessR, G, B and A are in ShapeDraw.h
#define kParamSoftness "softness"
#define kParamSoftnessLabel "Softness"
#define kParamSoftnessHint "Softness of the radial ramp. Draws an anti-aliased disc or ellipse if zero."

#define kParamAngle kParamRectangleInteractAngle
#define kParamAngleLabel kParamRectangleInteractAngleLabel
#define kParamAngleHint "Rotation angle of the ellipse around its center, in degrees (counter-clockwise)."

#define kParamPLinear "plinear"
#define kParamPLinearLabel "Perceptually Linear"
#define kParamPLinearHint "Make the radial ramp look more linear to the eye."

#define kParamColor0 "color0"
#define kParamColor0Label "Color 0"

#define kParamColor1 "color1"
#define kParamColor1Label "Color 1"

#define kParamExpandRoD "expandRoD"
#define kParamExpandRoDLabel "Expand RoD"
#define kParamExpandRoDHint "Expand the source region of definition by the shape RoD (if Source is connected and color0=(0,0,0,0))."

#ifdef OFX_EXTENSIONS_NATRON
#define OFX_COMPONENTS_OK(c) ((c)== ePixelComponentAlpha || (c) == ePixelComponentXY || (c) == ePixelComponentRGB || (c) == ePixelComponentRGBA)
#else
#define OFX_COMPONENTS_OK(c) ((c)== ePixelComponentAlpha || (c) == ePixelComponentRGB || (c) == ePixelComponentRGBA)
#endif

using ShapeDraw::RGBAValues;

class RadialProcessorBase
    : public ShapeDraw::ProcessorBase
{
protected:
    OfxPointD _btmLeft, _size;
    double _angle;
    double _softness;
    bool _plinear;

public:
    RadialProcessorBase(ImageEffect &instance)
        : ShapeDraw::ProcessorBase(instance)
        , _angle(0.)
        , _softness(1.)
        , _plinear(false)
    {
        _btmLeft.x = _btmLeft.y = _size.x = _size.y = 0.;
    }

    void setValues(const OfxPointD& btmLeft,
                   const OfxPointD& size,
                   double angle,
                   double softness,
                   bool plinear,
                   const RGBAValues& color0,
                   const RGBAValues& color1,
                   double mix,
                   bool processR,
                   bool processG,
                   bool processB,
                   bool processA,
                   const ShapeDraw::Options& drawOptions)
    {
        _btmLeft = btmLeft;
        _size = size;
        _angle = angle;
        _softness = (std::max)( 0., (std::min)(softness, 1.) );
        _plinear = plinear;
        _color0 = color0;
        _color1 = color1;
        _mix = mix;
        _processR = processR;
        _processG = processG;
        _processB = processB;
        _processA = processA;
        _drawOptions = drawOptions;
    }
};


template <class PIX, int nComponents, int maxValue>
class RadialProcessor
    : public RadialProcessorBase
{
public:
    RadialProcessor(ImageEffect &instance)
        : RadialProcessorBase(instance)
    {
    }

private:
    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
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
                        return process<true, true, true, true >(procWindow, rs); // RGBA
                    } else {
                        return process<true, true, true, false>(procWindow, rs); // RGBa
                    }
                } else {
                    if (a) {
                        return process<true, true, false, true >(procWindow, rs); // RGbA
                    } else {
                        return process<true, true, false, false>(procWindow, rs); // RGba
                    }
                }
            } else {
                if (b) {
                    if (a) {
                        return process<true, false, true, true >(procWindow, rs); // RgBA
                    } else {
                        return process<true, false, true, false>(procWindow, rs); // RgBa
                    }
                } else {
                    if (a) {
                        return process<true, false, false, true >(procWindow, rs); // RgbA
                    } else {
                        return process<true, false, false, false>(procWindow, rs); // Rgba
                    }
                }
            }
        } else {
            if (g) {
                if (b) {
                    if (a) {
                        return process<false, true, true, true >(procWindow, rs); // rGBA
                    } else {
                        return process<false, true, true, false>(procWindow, rs); // rGBa
                    }
                } else {
                    if (a) {
                        return process<false, true, false, true >(procWindow, rs); // rGbA
                    } else {
                        return process<false, true, false, false>(procWindow, rs); // rGba
                    }
                }
            } else {
                if (b) {
                    if (a) {
                        return process<false, false, true, true >(procWindow, rs); // rgBA
                    } else {
                        return process<false, false, true, false>(procWindow, rs); // rgBa
                    }
                } else {
                    if (a) {
                        return process<false, false, false, true >(procWindow, rs); // rgbA
                    } else {
                        return process<false, false, false, false>(procWindow, rs); // rgba
                    }
                }
            }
        }
#     endif // ifndef __COVERITY__
    } // multiThreadProcessImages

private:

    template<bool processR, bool processG, bool processB, bool processA>
    void process(const OfxRectI& procWindow, const OfxPointD& rs)
    {
        assert( (!processR && !processG && !processB) || (nComponents == 3 || nComponents == 4) );
        assert( !processA || (nComponents == 1 || nComponents == 4) );

        if (_angle != 0.) {
            return processRotated<processR, processG, processB, processA>(procWindow, rs);
        }

        float tmpPix[5] = {0.f, 0.f, 0.f, 0.f, 0.f}; // RGBA and the shape weight
        double par = _dstImg->getPixelAspectRatio();
        if (par <= 0.) {
            par = 1.;
        }

        // center of the ellipse
        OfxPointD c_canonical = { ( _btmLeft.x + (_btmLeft.x + _size.x) ) / 2, ( _btmLeft.y + (_btmLeft.y + _size.y) ) / 2 };
        // radius of the ellipse
        OfxPointD r_canonical = { _size.x / 2, _size.y / 2 };
        OfxPointD c; // center position in pixel
        Coords::toPixelSub(c_canonical, rs, par, &c);
        OfxPointD r; // radius in pixel
        r.x = r_canonical.x * rs.x / par;
        r.y = r_canonical.y * rs.y;
        // version 3: a Softness narrower than a pixel is drawn as Softness 0 (antialiased), as the tests/nuke
        // reference does
        const double softness = ( _pixelCenterSoftness && (_softness * (std::min)(r.x, r.y) < 1.) ) ? 0. : _softness;

        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);

                if ( _pixelCenterSoftness && (softness > 0.) ) {
                    // version 3: the Softness at the pixel center (pixel x is centered on x here). A Softness
                    // narrower than a pixel is drawn as Softness 0 (antialiased), as the tests/nuke reference does
                    const double dxc = (r.x > 0.) ? (x - c.x) / r.x : 1.;
                    const double dyc = (r.y > 0.) ? (y - c.y) / r.y : 1.;
                    const bool emptyShape = (r.x <= 0.) || (r.y <= 0.);
                    const float w = emptyShape ? 0.f :
                                    ShapeDraw::softWeight( _drawOptions.falloff, 1. - std::sqrt(dxc * dxc + dyc * dyc), softness, 0., _plinear );
                    setSoftPixel(w, tmpPix);
                    writePixel<PIX, nComponents, maxValue, processR, processG, processB, processA>(tmpPix, srcPix, x, y, dstPix);
                    continue;
                }

                // approximate subpixel rendering of the disc:
                // - test the pixel corner closer to the center. if it is outside, the pixel is fully outside
                // - test the pixel corner farther to the center. if it is inside, the pixel is fully outside
                // - else the pixel is mixed, and its value is (color0*abs(sqrt(dsq_farther)-1)+color1_smoothed*abs(sqrt(dsq_closer)-1))/(sqrt(dsq_farther)+sqrt(dsq_closer))
                OfxPointD p_closer = {(double)x, (double)y};
                OfxPointD p_farther = {(double)x, (double)y};

                if (x <= c.x - 0.5) {
                    p_closer.x += 0.5;
                    p_farther.x -= 0.5;
                } else if (x >= c.x + 0.5) {
                    p_closer.x -= 0.5;
                    p_farther.x += 0.5;
                }
                if (y <= c.y - 0.5) {
                    p_closer.y += 0.5;
                    p_farther.y -= 0.5;
                } else if (y >= c.y + 0.5) {
                    p_closer.y -= 0.5;
                    p_farther.y += 0.5;
                }
                // a zero or negative radius would give NaN or garbage: treat it as outside
                // (and do not divide by it)
                const bool empty = (r.x <= 0.) || (r.y <= 0.);
                double dx_closer = empty ? 0. : (p_closer.x - c.x) / r.x;
                double dy_closer = empty ? 0. : (p_closer.y - c.y) / r.y;
                double dx_farther = empty ? 0. : (p_farther.x - c.x) / r.x;
                double dy_farther = empty ? 0. : (p_farther.y - c.y) / r.y;

                if ( empty || (dx_closer >= 1) || (dy_closer >= 1) ) {
                    // outside
                    tmpPix[0] = (float)_color0.r;
                    tmpPix[1] = (float)_color0.g;
                    tmpPix[2] = (float)_color0.b;
                    tmpPix[3] = (float)_color0.a;
                    tmpPix[4] = 0.f; // the shape weight
                } else {
                    // maybe inside

                    //double dsq = dx * dx + dy * dy;
                    double dsq_closer = dx_closer * dx_closer + dy_closer * dy_closer;
                    double dsq_farther = dx_farther * dx_farther + dy_farther * dy_farther;
                    assert(dsq_closer <= dsq_farther);
                    if (dsq_closer > dsq_farther) {
                        // protect against bug
                        std::swap(dsq_closer, dsq_farther);
                    }
                    if (dsq_closer >= 1) {
                        // fully outside
                        tmpPix[0] = (float)_color0.r;
                        tmpPix[1] = (float)_color0.g;
                        tmpPix[2] = (float)_color0.b;
                        tmpPix[3] = (float)_color0.a;
                        tmpPix[4] = 0.f; // the shape weight
                    } else {
                        // always consider the value closest top the center to avoid discontinuities/artifacts
                        if ( (dsq_closer <= 0) || (softness == 0) ) {
                            // solid color
                            tmpPix[0] = (float)_color1.r;
                            tmpPix[1] = (float)_color1.g;
                            tmpPix[2] = (float)_color1.b;
                            tmpPix[3] = (float)_color1.a;
                            tmpPix[4] = 1.f;
                        } else {
                            // mixed
                            float t = ( 1.f - (float)std::sqrt( (std::max)(dsq_closer, 0.) ) ) / (float)softness;
                            if (t >= 1) {
                                tmpPix[0] = (float)_color1.r;
                                tmpPix[1] = (float)_color1.g;
                                tmpPix[2] = (float)_color1.b;
                                tmpPix[3] = (float)_color1.a;
                                tmpPix[4] = 1.f;
                            } else {
                                t = (float)ShapeDraw::falloff(_drawOptions.falloff, t);

                                if (_plinear) {
                                    // it seems to be the way Nuke does it... I could understand t*t, but why t*t*t?
                                    t = t * t * t;
                                }
                                tmpPix[0] = (float)_color0.r * (1.f - t) + (float)_color1.r * t;
                                tmpPix[1] = (float)_color0.g * (1.f - t) + (float)_color1.g * t;
                                tmpPix[2] = (float)_color0.b * (1.f - t) + (float)_color1.b * t;
                                tmpPix[3] = (float)_color0.a * (1.f - t) + (float)_color1.a * t;
                                tmpPix[4] = t;
                            }
                        }
                        float a;
                        if (dsq_farther <= 1) {
                            // fully inside
                            a = 1.;
                        } else {
                            // mixed pixel, partly inside / partly outside, center of pixel is outside
                            assert(dsq_closer < 1 && dsq_farther > 1);
                            // now mix with the outside pix;
                            a = ( 1 - std::sqrt( (std::max)(dsq_closer, 0.) ) ) / ( std::sqrt( (std::max)(dsq_farther, 0.) ) - std::sqrt( (std::max)(dsq_closer, 0.) ) );
                        }
                        assert(a >= 0. && a <= 1.);
                        if (a != 1.) {
                            tmpPix[0] = (float)_color0.r * (1.f - a) + tmpPix[0] * a;
                            tmpPix[1] = (float)_color0.g * (1.f - a) + tmpPix[1] * a;
                            tmpPix[2] = (float)_color0.b * (1.f - a) + tmpPix[2] * a;
                            tmpPix[3] = (float)_color0.a * (1.f - a) + tmpPix[3] * a;
                            tmpPix[4] *= a;
                        }
                    }
                }
                writePixel<PIX, nComponents, maxValue, processR, processG, processB, processA>(tmpPix, srcPix, x, y, dstPix);
            }
        }
    } // process

    // Render a rotated ellipse. The ellipse is rotated by _angle around its center, in canonical coordinates.
    // Antialiasing: the normalized distance to the center d is computed at the pixel center, and the
    // pixel footprint is approximated by d-h and d+h, where h is the half-extent of the pixel along the gradient of d.
    template<bool processR, bool processG, bool processB, bool processA>
    void processRotated(const OfxRectI& procWindow, const OfxPointD& rs)
    {
        float tmpPix[5] = {0.f, 0.f, 0.f, 0.f, 0.f}; // RGBA and the shape weight
        double par = _dstImg->getPixelAspectRatio();
        if (par <= 0.) {
            par = 1.;
        }

        // center of the ellipse (canonical)
        const OfxPointD c = { _btmLeft.x + _size.x / 2, _btmLeft.y + _size.y / 2 };
        // radius of the ellipse (canonical)
        const OfxPointD r = { _size.x / 2, _size.y / 2 };
        // size of a pixel (canonical)
        const OfxPointD pixSize = { par / rs.x, 1. / rs.y };
        const double angleRad = _angle * OFXS_PI / 180.;
        const double cosa = std::cos(angleRad);
        const double sina = std::sin(angleRad);
        const bool empty = (r.x <= 0.) || (r.y <= 0.);
        // version 3: a Softness narrower than a pixel is drawn as Softness 0, as in process()
        const double softness = ( _pixelCenterSoftness &&
                                  (_softness * (std::min)(r.x / pixSize.x, r.y / pixSize.y) < 1.) ) ? 0. : _softness;

        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            // pixel center (canonical), relative to the ellipse center
            const double py = (y + 0.5) * pixSize.y - c.y;

            for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                const double px = (x + 0.5) * pixSize.x - c.x;

                double d_closer = 1.;
                double d_farther = 1.;
                if (!empty) {
                    // coordinates in the ellipse frame, normalized by the radius
                    const double u = ( cosa * px + sina * py) / r.x;
                    const double v = (-sina * px + cosa * py) / r.y;
                    const double d = std::sqrt(u * u + v * v);
                    double h = 0.;
                    if (d > 0.) {
                        // gradient of d in the ellipse frame
                        const double gu = u / (d * r.x);
                        const double gv = v / (d * r.y);
                        // gradient of d in canonical coordinates
                        const double gx = cosa * gu - sina * gv;
                        const double gy = sina * gu + cosa * gv;
                        // half-extent of the pixel along the gradient
                        h = 0.5 * (std::abs(gx) * pixSize.x + std::abs(gy) * pixSize.y);
                    }
                    d_closer = (std::max)(d - h, 0.);
                    d_farther = d + h;
                    if ( _pixelCenterSoftness && (softness > 0.) ) {
                        // version 3: the Softness at the pixel center (narrower than a pixel: as Softness 0)
                        setSoftPixel(ShapeDraw::softWeight(_drawOptions.falloff, 1. - d, softness, 0., _plinear), tmpPix);
                        writePixel<PIX, nComponents, maxValue, processR, processG, processB, processA>(tmpPix, srcPix, x, y, dstPix);
                        continue;
                    }
                }

                if (d_closer >= 1) {
                    // fully outside
                    tmpPix[0] = (float)_color0.r;
                    tmpPix[1] = (float)_color0.g;
                    tmpPix[2] = (float)_color0.b;
                    tmpPix[3] = (float)_color0.a;
                    tmpPix[4] = 0.f; // the shape weight
                } else {
                    // always consider the value closest top the center to avoid discontinuities/artifacts
                    if ( (d_closer <= 0) || (softness == 0) ) {
                        // solid color
                        tmpPix[0] = (float)_color1.r;
                        tmpPix[1] = (float)_color1.g;
                        tmpPix[2] = (float)_color1.b;
                        tmpPix[3] = (float)_color1.a;
                        tmpPix[4] = 1.f;
                    } else {
                        // mixed
                        float t = ( 1.f - (float)d_closer ) / (float)softness;
                        if (t >= 1) {
                            tmpPix[0] = (float)_color1.r;
                            tmpPix[1] = (float)_color1.g;
                            tmpPix[2] = (float)_color1.b;
                            tmpPix[3] = (float)_color1.a;
                            tmpPix[4] = 1.f;
                        } else {
                            t = (float)ShapeDraw::falloff(_drawOptions.falloff, t);

                            if (_plinear) {
                                t = t * t * t;
                            }
                            tmpPix[0] = (float)_color0.r * (1.f - t) + (float)_color1.r * t;
                            tmpPix[1] = (float)_color0.g * (1.f - t) + (float)_color1.g * t;
                            tmpPix[2] = (float)_color0.b * (1.f - t) + (float)_color1.b * t;
                            tmpPix[3] = (float)_color0.a * (1.f - t) + (float)_color1.a * t;
                            tmpPix[4] = t;
                        }
                    }
                    if (d_farther > 1) {
                        // mixed pixel, partly inside / partly outside
                        const float a = (float)( (1. - d_closer) / (d_farther - d_closer) );
                        tmpPix[0] = (float)_color0.r * (1.f - a) + tmpPix[0] * a;
                        tmpPix[1] = (float)_color0.g * (1.f - a) + tmpPix[1] * a;
                        tmpPix[2] = (float)_color0.b * (1.f - a) + tmpPix[2] * a;
                        tmpPix[3] = (float)_color0.a * (1.f - a) + tmpPix[3] * a;
                        tmpPix[4] *= a;
                    }
                }
                writePixel<PIX, nComponents, maxValue, processR, processG, processB, processA>(tmpPix, srcPix, x, y, dstPix);
            }
        }
    } // processRotated
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class RadialPlugin
    : public GeneratorPlugin
{
public:
    /** @brief ctor */
    RadialPlugin(OfxImageEffectHandle handle,
                 unsigned int majorVersion)
        : GeneratorPlugin(handle, false, kSupportsByte, kSupportsUShort, kSupportsHalf, kSupportsFloat)
        , _majorVersion(majorVersion)
        , _srcClip(NULL)
        , _processR(NULL)
        , _processG(NULL)
        , _processB(NULL)
        , _processA(NULL)
        , _angle(NULL)
        , _color0(NULL)
        , _color1(NULL)
        , _expandRoD(NULL)
        , _draw(*this, kParamColor0, kParamColor1)
    {

        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || OFX_COMPONENTS_OK(_srcClip->getPixelComponents())) ) );
        _maskClip = fetchClip(getContext() == eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || !_maskClip->isConnected() || _maskClip->getPixelComponents() == ePixelComponentAlpha);
        _processR = fetchBooleanParam(kParamProcessR);
        _processG = fetchBooleanParam(kParamProcessG);
        _processB = fetchBooleanParam(kParamProcessB);
        _processA = fetchBooleanParam(kParamProcessA);
        assert(_processR && _processG && _processB && _processA);
        _angle = fetchDoubleParam(kParamAngle);
        _softness = fetchDoubleParam(kParamSoftness);
        _plinear = fetchBooleanParam(kParamPLinear);
        _color0 = fetchRGBAParam(kParamColor0);
        _color1 = fetchRGBAParam(kParamColor1);
        _expandRoD = fetchBooleanParam(kParamExpandRoD);
        assert(_angle && _softness && _plinear && _color0 && _color1 && _expandRoD);

        _mix = fetchDoubleParam(kParamMix);
        _maskApply = ( ofxsMaskIsAlwaysConnected( OFX::getImageEffectHostDescription() ) && paramExists(kParamMaskApply) ) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
    }

private:
    /* override is identity */
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;

    /** @brief true if Process A is checked and the Source is Opaque (the Output is then UnPremultiplied) */
    bool outputIsUnpremultipliedFromOpaque() const;

    /* Override the clip preferences */
    void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    template <int nComponents>
    void renderInternal(const RenderArguments &args, BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(RadialProcessorBase &, const RenderArguments &args);

    virtual Clip* getSrcClip() const OVERRIDE FINAL
    {
        return _srcClip;
    }

private:
    const unsigned int _majorVersion;

    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_srcClip;
    Clip *_maskClip;
    BooleanParam* _processR;
    BooleanParam* _processG;
    BooleanParam* _processB;
    BooleanParam* _processA;
    DoubleParam* _angle;
    DoubleParam* _softness;
    BooleanParam* _plinear;
    RGBAParam* _color0;
    RGBAParam* _color1;
    BooleanParam* _expandRoD;
    DoubleParam* _mix;
    BooleanParam* _maskApply;
    BooleanParam* _maskInvert;
    ShapeDraw::Params _draw; // the Draw options and the colors
};

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
RadialPlugin::setupAndProcess(RadialProcessorBase &processor,
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
        checkBadRenderScaleOrField(mask, args);
        bool maskInvert = _maskInvert->getValueAtTime(time);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    if ( src.get() && dst.get() ) {
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();
        BitDepthEnum dstBitDepth       = dst->getPixelDepth();
        PixelComponentEnum dstComponents  = dst->getPixelComponents();

        // see if they have the same depths and bytes and all
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    // set the images
    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );

    // set the render window
    processor.setRenderWindow(args.renderWindow, args.renderScale);

    OfxPointD btmLeft, size;
    {
        OfxRectD rod;
        bool wasCaught = GeneratorPlugin::getRegionOfDefinition(time, rod);
        if (!wasCaught) {
            //Overlay in default mode, use the project rod
            size = getProjectSize();
            btmLeft = getProjectOffset();
        } else {
            btmLeft.x = rod.x1;
            btmLeft.y = rod.y1;
            size.x = rod.x2 - rod.x1;
            size.y = rod.y2 - rod.y1;
        }
    }
    double angle = _angle->getValueAtTime(time);
    double softness = _softness->getValueAtTime(time);
    bool plinear = _plinear->getValueAtTime(time);
    const ShapeDraw::Colors<RGBAValues> colors = _draw.colors<RGBAValues>(time);
    const RGBAValues& color0 = colors.color0;
    const RGBAValues& color1 = colors.color1;

    bool processR = _processR->getValueAtTime(time);
    bool processG = _processG->getValueAtTime(time);
    bool processB = _processB->getValueAtTime(time);
    bool processA = _processA->getValueAtTime(time);
    double mix = _mix->getValueAtTime(time);

    processor.setValues(btmLeft, size,
                        angle,
                        softness, plinear,
                        color0, color1,
                        mix,
                        processR, processG, processB, processA,
                        _draw.options(time));
    processor.setPixelCenterSoftness(_majorVersion >= kVersionPixelCenterSoftness);
    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // RadialPlugin::setupAndProcess

// the internal render function
template <int nComponents>
void
RadialPlugin::renderInternal(const RenderArguments &args,
                             BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case eBitDepthUByte: {
        RadialProcessor<unsigned char, nComponents, 255> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthUShort: {
        RadialProcessor<unsigned short, nComponents, 65535> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthFloat: {
        RadialProcessor<float, nComponents, 1> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
RadialPlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(OFX_COMPONENTS_OK(dstComponents));
    if (dstComponents == ePixelComponentRGBA) {
        renderInternal<4>(args, dstBitDepth);
    } else if (dstComponents == ePixelComponentRGB) {
        renderInternal<3>(args, dstBitDepth);
#ifdef OFX_EXTENSIONS_NATRON
    } else if (dstComponents == ePixelComponentXY) {
        renderInternal<2>(args, dstBitDepth);
#endif
    } else {
        assert(dstComponents == ePixelComponentAlpha);
        renderInternal<1>(args, dstBitDepth);
    }
}

bool
RadialPlugin::isIdentity(const IsIdentityArguments &args,
                         Clip * &identityClip,
                         double &identityTime
                         , int& view, std::string& plane)
{
    if ( GeneratorPlugin::isIdentity(args, identityClip, identityTime, view, plane) ) {
        return true;
    }

    if (!_srcClip || !_srcClip->isConnected()) {
        return false;
    }
    if ( outputIsUnpremultipliedFromOpaque() ) {
        // the Source is Opaque but the Output is UnPremultiplied (see getClipPreferences()): a
        // pass-through would keep the Source premultiplication (issue #328)
        return false;
    }
    if ( _draw.outputIsPremultipliedByShape(_srcClip, _maskClip) ) {
        // the Output is PreMultiplied (see getClipPreferences()), also where Mix is 0
        return false;
    }
    const double time = args.time;
    double mix = _mix->getValueAtTime(time);

    if (mix == 0. /*|| (!processR && !processG && !processB && !processA)*/) {
        identityClip = _srcClip;

        return true;
    }

    if ( !_draw.changesSource(time, _srcClip) ) {
        bool processR = _processR->getValueAtTime(time);
        bool processG = _processG->getValueAtTime(time);
        bool processB = _processB->getValueAtTime(time);
        bool processA = _processA->getValueAtTime(time);
        if (!processR && !processG && !processB && !processA) {
            identityClip = _srcClip;

            return true;
        }
    }

    const ShapeDraw::Colors<RGBAValues> colors = _draw.colors<RGBAValues>(time);
    const RGBAValues& color0 = colors.color0;
    const RGBAValues& color1 = colors.color1;
    if ( (color0.r == 0.) && (color0.g == 0.) && (color0.b == 0.) && (color0.a == 0.) &&
         (color1.r == 0.) && (color1.g == 0.) && (color1.b == 0.) && (color1.a == 0.) &&
         !_draw.changesSource(time, _srcClip) ) {
        identityClip = _srcClip;

        return true;
    }

    if ( ofxsMaskRoDExcludesRenderWindow(args, _maskClip, _maskApply, _maskInvert) ) {
        identityClip = _srcClip;

        return true;
    }

    return false;
} // RadialPlugin::isIdentity

// true if Process A is checked and the Source is Opaque: the output alpha is changed, and the Output
// is UnPremultiplied. Used by getClipPreferences() and isIdentity().
bool
RadialPlugin::outputIsUnpremultipliedFromOpaque() const
{
    return _processA->getValue() && _srcClip && _srcClip->isConnected() &&
           (_srcClip->getPreMultiplication() == eImageOpaque);
}

/* Override the clip preferences */
void
RadialPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    if (_srcClip) {
        // set the premultiplication of _dstClip if alpha is affected and source is Opaque
        // Unfortunately, we cannot check the output components as was done in
        // https://github.com/NatronGitHub/openfx-misc/commit/844a442b5baeef4b1e1a0fd4d5e957707f4465ca
        // since it would call getClipPrefs recursively.
        // We just set the output components.
        if ( outputIsUnpremultipliedFromOpaque() ) {
            clipPreferences.setClipComponents(*_dstClip, ePixelComponentRGBA);
            clipPreferences.setClipComponents(*_srcClip, ePixelComponentRGBA);
            clipPreferences.setOutputPremultiplication(eImageUnPreMultiplied);
        }
        if ( _draw.outputIsPremultipliedByShape(_srcClip, _maskClip) ) {
            // the output alpha is the shape, and the Source RGB are multiplied by it
            clipPreferences.setOutputPremultiplication(eImagePreMultiplied);
        }
    }

    // if no input is connected, output is continuous
    if ( !_srcClip || !_srcClip->isConnected() ) {
        clipPreferences.setOutputHasContinuousSamples(true);
    }

    GeneratorPlugin::getClipPreferences(clipPreferences);
}

bool
RadialPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                    OfxRectD &rod)
{
    const double time = args.time;
    double mix = _mix->getValueAtTime(time);

    if (mix == 0.) {
        if ( _srcClip && _srcClip->isConnected() ) {
            // nothing to draw: return default region of definition
            return false;
        } else {
            // empty RoD
            rod.x1 = rod.y1 = rod.x2 = rod.y2 = 0.;

            return true;
        }
    }
    const ShapeDraw::Colors<RGBAValues> colors = _draw.colors<RGBAValues>(time);
    const RGBAValues& color0 = colors.color0;
    const RGBAValues& color1 = colors.color1;
    GeneratorExtentEnum extent = (GeneratorExtentEnum)_extent->getValue();
    if ( (extent != eGeneratorExtentFormat) &&
         ( (color0.r != 0.) || (color0.g != 0.) || (color0.b != 0.) || (color0.a != 0.) ) ) {
        // something has to be drawn outside of the rectangle

        // return default RoD.
        return false;
        //// Other option: RoD could be union(defaultRoD,inputsRoD)
        //// Natron does this if the RoD is infinite
        //rod.x1 = rod.y1 = kOfxFlagInfiniteMin;
        //rod.x2 = rod.y2 = kOfxFlagInfiniteMax;
    }
    // nothing to draw if both colors are black and transparent (with Extent = Format, Color 0 may not
    // be: then the format is drawn, below)
    if ( (color1.r == 0.) && (color1.g == 0.) && (color1.b == 0.) && (color1.a == 0.) &&
         (color0.r == 0.) && (color0.g == 0.) && (color0.b == 0.) && (color0.a == 0.) ) {
        if ( _srcClip && _srcClip->isConnected() ) { // _srcClip is NULL in the Generator context
            // nothing to draw: return default region of definition
            return false;
        } else {
            // empty RoD
            rod.x1 = rod.y1 = rod.x2 = rod.y2 = 0.;

            return true;
        }
    }
    bool expandRoD = _expandRoD->getValueAtTime(time);
    if (_srcClip && _srcClip->isConnected() && !expandRoD) {
        return false;
    }

    bool wasCaught = GeneratorPlugin::getRegionOfDefinition(time, rod);
    if ( wasCaught && (extent != eGeneratorExtentFormat) ) {
        double angle = _angle->getValueAtTime(time);
        if (angle != 0.) {
            // bounding box of the rotated ellipse (the softness is inside the ellipse)
            OfxPointD c = { (rod.x1 + rod.x2) / 2, (rod.y1 + rod.y2) / 2 };
            OfxPointD r = { (rod.x2 - rod.x1) / 2, (rod.y2 - rod.y1) / 2 };
            double a = angle * OFXS_PI / 180.;
            double cosa = std::cos(a);
            double sina = std::sin(a);
            double hx = std::sqrt(r.x * r.x * cosa * cosa + r.y * r.y * sina * sina);
            double hy = std::sqrt(r.x * r.x * sina * sina + r.y * r.y * cosa * cosa);
            rod.x1 = c.x - hx;
            rod.x2 = c.x + hx;
            rod.y1 = c.y - hy;
            rod.y2 = c.y + hy;
        }
        // add one pixel in each direction to ensure border is black and transparent
        // (non-black+transparent case was treated above)
        rod.x1 -= 1;
        rod.y1 -= 1;
        rod.x2 += 1;
        rod.y2 += 1;
    }
    if ( _srcClip && _srcClip->isConnected() ) {
        // something has to be drawn outside of the rectangle: return union of input RoD and rectangle
        const OfxRectD& srcRoD = _srcClip->getRegionOfDefinition(time);
        Coords::rectBoundingBox(rod, srcRoD, &rod);
    } else if (!wasCaught) {
        //The generator is in default mode, if the source clip is connected, take its rod, otherwise take
        //the rod of the project
        OfxPointD siz = getProjectSize();
        OfxPointD off = getProjectOffset();
        rod.x1 = off.x;
        rod.x2 = off.x + siz.x;
        rod.y1 = off.y;
        rod.y2 = off.y + siz.y;
    }

    return true;
} // RadialPlugin::getRegionOfDefinition

mDeclarePluginFactoryVersioned(RadialPluginFactory, {ofxsThreadSuiteCheck();}, {});
template<unsigned int majorVersion>
void
RadialPluginFactory<majorVersion>::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);
    desc.setIsObsolete(majorVersion < kPluginVersionMajor);

    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextGenerator);
    if (kSupportsByte) {
        desc.addSupportedBitDepth(eBitDepthUByte);
    }
    if (kSupportsUShort) {
        desc.addSupportedBitDepth(eBitDepthUShort);
    }
    if (kSupportsFloat) {
        desc.addSupportedBitDepth(eBitDepthFloat);
    }


    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);

    desc.setSupportsTiles(kSupportsTiles);

    // in order to support multiresolution, render() must take into account the pixelaspectratio and the renderscale
    // and scale the transform appropriately.
    // All other functions are usually in canonical coordinates.
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    generatorDescribe(desc, true, eGeneratorOverlayDrawSuite); // the overlay is rotated by the Angle parameter; V2 (DrawSuite) and V1 (OpenGL)

#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone); // we have our own channel selector
#endif
}

template<unsigned int majorVersion>
ImageEffect*
RadialPluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                                  ContextEnum /*context*/)
{
    return new RadialPlugin(handle, majorVersion);
}

template<unsigned int majorVersion>
void
RadialPluginFactory<majorVersion>::describeInContext(ImageEffectDescriptor &desc,
                                       ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    // always declare the source clip first, because some hosts may consider
    // it as the default input clip (e.g. Nuke)
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    srcClip->addSupportedComponent(ePixelComponentXY);
#endif
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);
    srcClip->setOptional(true);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    dstClip->addSupportedComponent(ePixelComponentXY);
#endif
    dstClip->addSupportedComponent(ePixelComponentAlpha);
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
        // triggers the clip preferences (see ShapeDraw::Params::outputIsPremultipliedByShape()), and may still
        // animate, as in Natron 2.5.0: the declared state follows the time of the last clip preferences call
        desc.addClipPreferencesSlaveParam(*param);
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
        // triggers the clip preferences (see ShapeDraw::Params::outputIsPremultipliedByShape()), and may still
        // animate, as in Natron 2.5.0: the declared state follows the time of the last clip preferences call
        desc.addClipPreferencesSlaveParam(*param);
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
        // triggers the clip preferences (see ShapeDraw::Params::outputIsPremultipliedByShape()), and may still
        // animate, as in Natron 2.5.0: the declared state follows the time of the last clip preferences call
        desc.addClipPreferencesSlaveParam(*param);
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
        param->setDefault(true);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }

    ShapeDraw::describeParams(desc, page);

    generatorDescribeInContext(page, desc, *dstClip, eGeneratorExtentSize, ePixelComponentRGBA, false,  context);

    // angle
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamAngle);
        param->setLabel(kParamAngleLabel);
        param->setHint(kParamAngleHint);
        param->setDoubleType(eDoubleTypeAngle);
        param->setDefault(0.);
        param->setDisplayRange(-180., 180.);
        param->setIncrement(0.1);
        if (page) {
            page->addChild(*param);
        }
    }

    // softness
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSoftness);
        param->setLabel(kParamSoftnessLabel);
        param->setHint(kParamSoftnessHint);
        param->setDefault(1.);
        param->setIncrement(0.01);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        param->setDigits(2);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }

    // plinear
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamPLinear);
        param->setLabel(kParamPLinearLabel);
        param->setHint(kParamPLinearHint);
        if (page) {
            page->addChild(*param);
        }
    }

    // falloff
    ShapeDraw::describeFalloffParam(desc, page, (majorVersion >= kVersionPixelCenterSoftness) ? ShapeDraw::FalloffEnum::eFalloffSmooth : ShapeDraw::FalloffEnum::eFalloffQuadratic);

    // color1
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamColor1);
        desc.addClipPreferencesSlaveParam(*param); // its alpha: see ShapeDraw::Params::outputIsPremultipliedByShape()
        param->setLabel(kParamColor1Label);
        param->setDefault(1., 1., 1., 1. );
        if (page) {
            page->addChild(*param);
        }
    }

    // color0
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamColor0);
        desc.addClipPreferencesSlaveParam(*param); // its alpha, and the RGB of Color 0: see ShapeDraw::Params::outputIsPremultipliedByShape()
        param->setLabel(kParamColor0Label);
        param->setDefault(0, 0, 0, 0);
        if (page) {
            page->addChild(*param);
        }
    }

    // expandRoD
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamExpandRoD);
        param->setLabel(kParamExpandRoDLabel);
        param->setHint(kParamExpandRoDHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }
} // RadialPluginFactory::describeInContext

static RadialPluginFactory<kPluginVersionMajor> p(kPluginIdentifier, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)
// 2.2: the Softness at the pixel corner nearest to the center, Quadratic by default
static RadialPluginFactory<2> p_v2(kPluginIdentifier, kPluginVersionMinorV2);
mRegisterPluginFactoryInstance(p_v2)

OFXS_NAMESPACE_ANONYMOUS_EXIT
