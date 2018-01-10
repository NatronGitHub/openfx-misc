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
 * OFX Radial plugin.
 */

// NOTE: This plugin is very similar to Rectangle. Any changes made here should probably be made in Rectangle.

#include <cmath>
#include <algorithm>

#include "ofxsProcessing.H"
#include "ofxsCoords.h"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"
#include "ofxsGenerator.h"
#ifdef OFX_EXTENSIONS_NATRON
#include "ofxNatron.h"
#endif
#include "ofxsThreadSuite.h"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
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
    "- The selected extent plus a one-pixel border if Color0 is black and transparent.\n" \
    "\n" \
    "See also: http://opticalenquiry.com/nuke/index.php?title=Radial"

#define kPluginIdentifier "net.sf.openfx.Radial"
// History:
// version 1.0: initial version
// version 2.0: use kNatronOfxParamProcess* parameters
// version 2.1: antialiased render
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 1 // Increment this when you have fixed a bug or made it faster.

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
#define kParamSoftness "softness"
#define kParamSoftnessLabel "Softness"
#define kParamSoftnessHint "Softness of the radial ramp. Draws an anti-aliased disc or ellipse if zero."

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


struct RGBAValues
{
    double r, g, b, a;
    RGBAValues(double v) : r(v), g(v), b(v), a(v) {}

    RGBAValues() : r(0), g(0), b(0), a(0) {}
};

static inline
double
rampSmooth(double t)
{
    t *= 2.;
    if (t < 1) {
        return t * t / (2.);
    } else {
        t -= 1.;

        return -0.5 * (t * (t - 2) - 1);
    }
}

class RadialProcessorBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg;
    const Image *_maskImg;
    bool _doMasking;
    double _mix;
    bool _maskInvert;
    bool _processR, _processG, _processB, _processA;
    OfxPointD _btmLeft, _size;
    double _softness;
    bool _plinear;
    RGBAValues _color0, _color1;

public:
    RadialProcessorBase(ImageEffect &instance)
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
        , _softness(1.)
        , _plinear(false)
    {
        _btmLeft.x = _btmLeft.y = _size.x = _size.y = 0.;
        _color0.r = _color0.g = _color0.b = _color0.a = 0.;
        _color1.r = _color1.g = _color1.b = _color1.a = 0.;
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

    void setValues(const OfxPointD& btmLeft,
                   const OfxPointD& size,
                   double softness,
                   bool plinear,
                   const RGBAValues& color0,
                   const RGBAValues& color1,
                   double mix,
                   bool processR,
                   bool processG,
                   bool processB,
                   bool processA)
    {
        _btmLeft = btmLeft;
        _size = size;
        _softness = std::max( 0., std::min(softness, 1.) );
        _plinear = plinear;
        _color0 = color0;
        _color1 = color1;
        _mix = mix;
        _processR = processR;
        _processG = processG;
        _processB = processB;
        _processA = processA;
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
    void process(const OfxRectI& procWindow)
    {
        assert( (!processR && !processG && !processB) || (nComponents == 3 || nComponents == 4) );
        assert( !processA || (nComponents == 1 || nComponents == 4) );

        float tmpPix[4];
        OfxPointD rs = _dstImg->getRenderScale();
        double par = _dstImg->getPixelAspectRatio();

        // center of the ellipse
        OfxPointD c_canonical = { ( _btmLeft.x + (_btmLeft.x + _size.x) ) / 2, ( _btmLeft.y + (_btmLeft.y + _size.y) ) / 2 };
        // radius of the ellipse
        OfxPointD r_canonical = { _size.x / 2, _size.y / 2 };
        OfxPointD c; // center position in pixel
        Coords::toPixelSub(c_canonical, rs, par, &c);
        OfxPointD r; // radius in pixel
        r.x = r_canonical.x * rs.x / par;
        r.y = r_canonical.y * rs.y;

        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);

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
                double dx_closer = (p_closer.x - c.x) / r.x;
                double dy_closer = (p_closer.y - c.y) / r.y;
                double dx_farther = (p_farther.x - c.x) / r.x;
                double dy_farther = (p_farther.y - c.y) / r.y;


                if ( (dx_closer >= 1) || (dy_closer >= 1) ) {
                    // outside
                    tmpPix[0] = (float)_color0.r;
                    tmpPix[1] = (float)_color0.g;
                    tmpPix[2] = (float)_color0.b;
                    tmpPix[3] = (float)_color0.a;
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
                    } else {
                        // always consider the value closest top the center to avoid discontinuities/artifacts
                        if ( (dsq_closer <= 0) || (_softness == 0) ) {
                            // solid color
                            tmpPix[0] = (float)_color1.r;
                            tmpPix[1] = (float)_color1.g;
                            tmpPix[2] = (float)_color1.b;
                            tmpPix[3] = (float)_color1.a;
                        } else {
                            // mixed
                            float t = ( 1.f - (float)std::sqrt( std::max(dsq_closer, 0.) ) ) / (float)_softness;
                            if (t >= 1) {
                                tmpPix[0] = (float)_color1.r;
                                tmpPix[1] = (float)_color1.g;
                                tmpPix[2] = (float)_color1.b;
                                tmpPix[3] = (float)_color1.a;
                            } else {
                                t = (float)rampSmooth(t);

                                if (_plinear) {
                                    // it seems to be the way Nuke does it... I could understand t*t, but why t*t*t?
                                    t = t * t * t;
                                }
                                tmpPix[0] = (float)_color0.r * (1.f - t) + (float)_color1.r * t;
                                tmpPix[1] = (float)_color0.g * (1.f - t) + (float)_color1.g * t;
                                tmpPix[2] = (float)_color0.b * (1.f - t) + (float)_color1.b * t;
                                tmpPix[3] = (float)_color0.a * (1.f - t) + (float)_color1.a * t;
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
                            a = ( 1 - std::sqrt( std::max(dsq_closer, 0.) ) ) / ( std::sqrt( std::max(dsq_farther, 0.) ) - std::sqrt( std::max(dsq_closer, 0.) ) );
                        }
                        assert(a >= 0. && a <= 1.);
                        if (a != 1.) {
                            tmpPix[0] = (float)_color0.r * (1.f - a) + tmpPix[0] * a;
                            tmpPix[1] = (float)_color0.g * (1.f - a) + tmpPix[1] * a;
                            tmpPix[2] = (float)_color0.b * (1.f - a) + tmpPix[2] * a;
                            tmpPix[3] = (float)_color0.a * (1.f - a) + tmpPix[3] * a;
                        }
                    }
                }
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
                if (processR) {
                    tmpPix[0] = tmpPix[0] + srcPixRGBA[0] * (1.f - a);
                } else {
                    tmpPix[0] = srcPixRGBA[0];
                }
                if (processG) {
                    tmpPix[1] = tmpPix[1] + srcPixRGBA[1] * (1.f - a);
                } else {
                    tmpPix[1] = srcPixRGBA[1];
                }
                if (processB) {
                    tmpPix[2] = tmpPix[2] + srcPixRGBA[2] * (1.f - a);
                } else {
                    tmpPix[2] = srcPixRGBA[2];
                }
                if (processA) {
                    tmpPix[3] = tmpPix[3] + srcPixRGBA[3] * (1.f - a);
                } else {
                    tmpPix[3] = srcPixRGBA[3];
                }
                ofxsMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
            }
        }
    } // process
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class RadialPlugin
    : public GeneratorPlugin
{
public:
    /** @brief ctor */
    RadialPlugin(OfxImageEffectHandle handle)
        : GeneratorPlugin(handle, false, kSupportsByte, kSupportsUShort, kSupportsHalf, kSupportsFloat)
        , _srcClip(NULL)
        , _processR(NULL)
        , _processG(NULL)
        , _processB(NULL)
        , _processA(NULL)
        , _color0(NULL)
        , _color1(NULL)
        , _expandRoD(NULL)
    {
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentRGBA ||
                               _srcClip->getPixelComponents() == ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentXY ||
                               _srcClip->getPixelComponents() == ePixelComponentAlpha) ) );
        _maskClip = fetchClip(getContext() == eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || !_maskClip->isConnected() || _maskClip->getPixelComponents() == ePixelComponentAlpha);
        _processR = fetchBooleanParam(kParamProcessR);
        _processG = fetchBooleanParam(kParamProcessG);
        _processB = fetchBooleanParam(kParamProcessB);
        _processA = fetchBooleanParam(kParamProcessA);
        assert(_processR && _processG && _processB && _processA);
        _softness = fetchDoubleParam(kParamSoftness);
        _plinear = fetchBooleanParam(kParamPLinear);
        _color0 = fetchRGBAParam(kParamColor0);
        _color1 = fetchRGBAParam(kParamColor1);
        _expandRoD = fetchBooleanParam(kParamExpandRoD);
        assert(_softness && _plinear && _color0 && _color1 && _expandRoD);

        _mix = fetchDoubleParam(kParamMix);
        _maskApply = ( ofxsMaskIsAlwaysConnected( OFX::getImageEffectHostDescription() ) && paramExists(kParamMaskApply) ) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
    }

private:
    /* override is identity */
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;

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

    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_srcClip;
    Clip *_maskClip;
    BooleanParam* _processR;
    BooleanParam* _processG;
    BooleanParam* _processB;
    BooleanParam* _processA;
    DoubleParam* _softness;
    BooleanParam* _plinear;
    RGBAParam* _color0;
    RGBAParam* _color1;
    BooleanParam* _expandRoD;
    DoubleParam* _mix;
    BooleanParam* _maskApply;
    BooleanParam* _maskInvert;
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
    if (doMasking) {
        if ( mask.get() ) {
            if ( (mask->getRenderScale().x != args.renderScale.x) ||
                 ( mask->getRenderScale().y != args.renderScale.y) ||
                 ( ( mask->getField() != eFieldNone) /* for DaVinci Resolve */ && ( mask->getField() != args.fieldToRender) ) ) {
                setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                throwSuiteStatusException(kOfxStatFailed);
            }
        }
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
    processor.setRenderWindow(args.renderWindow);

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
    double softness = _softness->getValueAtTime(time);
    bool plinear = _plinear->getValueAtTime(time);
    RGBAValues color0, color1;
    _color0->getValueAtTime(time, color0.r, color0.g, color0.b, color0.a);
    _color1->getValueAtTime(time, color1.r, color1.g, color1.b, color1.a);

    bool processR = _processR->getValueAtTime(time);
    bool processG = _processG->getValueAtTime(time);
    bool processB = _processB->getValueAtTime(time);
    bool processA = _processA->getValueAtTime(time);
    double mix = _mix->getValueAtTime(time);

    processor.setValues(btmLeft, size,
                        softness, plinear,
                        color0, color1,
                        mix,
                        processR, processG, processB, processA);
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

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(dstComponents == ePixelComponentRGBA || dstComponents == ePixelComponentRGB || dstComponents == ePixelComponentXY || dstComponents == ePixelComponentAlpha);
    if (dstComponents == ePixelComponentRGBA) {
        renderInternal<4>(args, dstBitDepth);
    } else if (dstComponents == ePixelComponentRGB) {
        renderInternal<3>(args, dstBitDepth);
    } else if (dstComponents == ePixelComponentXY) {
        renderInternal<2>(args, dstBitDepth);
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
    const double time = args.time;
    double mix = _mix->getValueAtTime(time);

    if (mix == 0. /*|| (!processR && !processG && !processB && !processA)*/) {
        identityClip = _srcClip;

        return true;
    }

    {
        bool processR = _processR->getValueAtTime(time);
        bool processG = _processG->getValueAtTime(time);
        bool processB = _processB->getValueAtTime(time);
        bool processA = _processA->getValueAtTime(time);
        if (!processR && !processG && !processB && !processA) {
            identityClip = _srcClip;

            return true;
        }
    }

    RGBAValues color0, color1;
    _color0->getValueAtTime(time, color0.r, color0.g, color0.b, color0.a);
    _color1->getValueAtTime(time, color1.r, color1.g, color1.b, color1.a);
    if ( (color0.r == 0.) && (color0.g == 0.) && (color0.b == 0.) && (color0.a == 0.) &&
         (color1.r == 0.) && (color1.g == 0.) && (color1.b == 0.) && (color1.a == 0.) ) {
        identityClip = _srcClip;

        return true;
    }

    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    if (doMasking) {
        bool maskInvert = _maskInvert->getValueAtTime(time);
        if (!maskInvert) {
            OfxRectI maskRoD;
            if (getImageEffectHostDescription()->supportsMultiResolution) {
                // In Sony Catalyst Edit, clipGetRegionOfDefinition returns the RoD in pixels instead of canonical coordinates.
                // In hosts that do not support multiResolution (e.g. Sony Catalyst Edit), all inputs have the same RoD anyway.
                Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
                // effect is identity if the renderWindow doesn't intersect the mask RoD
                if ( !Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0) ) {
                    identityClip = _srcClip;

                    return true;
                }
            }
        }
    }

    return false;
} // RadialPlugin::isIdentity

/* Override the clip preferences */
void
RadialPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    if (_srcClip) {
        // set the premultiplication of _dstClip if alpha is affected and source is Opaque
        bool processA = _processA->getValue();
        // Unfortunately, we cannot check the output components as was done in
        // https://github.com/devernay/openfx-misc/commit/844a442b5baeef4b1e1a0fd4d5e957707f4465ca
        // since it would call getClipPrefs recursively.
        // We just set the output components.
        if ( processA && _srcClip && _srcClip->isConnected() && _srcClip->getPreMultiplication() == eImageOpaque) {
            clipPreferences.setClipComponents(*_dstClip, ePixelComponentRGBA);
            clipPreferences.setClipComponents(*_srcClip, ePixelComponentRGBA);
            clipPreferences.setOutputPremultiplication(eImageUnPreMultiplied);
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
    RGBAValues color0;
    _color0->getValueAtTime(time, color0.r, color0.g, color0.b, color0.a);
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
    RGBAValues color1;
    _color1->getValueAtTime(time, color1.r, color1.g, color1.b, color1.a);
    if ( (color1.r == 0.) && (color1.g == 0.) && (color1.b == 0.) && (color1.a == 0.) ) {
        if ( _srcClip->isConnected() ) {
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

mDeclarePluginFactory(RadialPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
RadialPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

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
    generatorDescribe(desc);

#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone); // we have our own channel selector
#endif
}

ImageEffect*
RadialPluginFactory::createInstance(OfxImageEffectHandle handle,
                                    ContextEnum /*context*/)
{
    return new RadialPlugin(handle);
}

void
RadialPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                       ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    // always declare the source clip first, because some hosts may consider
    // it as the default input clip (e.g. Nuke)
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentXY);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);
    srcClip->setOptional(true);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentXY);
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
        param->setDefault(true);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }

    generatorDescribeInContext(page, desc, *dstClip, eGeneratorExtentSize, ePixelComponentRGBA, false,  context);

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

    // color0
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamColor0);
        param->setLabel(kParamColor0Label);
        param->setDefault(0, 0, 0, 0);
        if (page) {
            page->addChild(*param);
        }
    }

    // color1
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamColor1);
        param->setLabel(kParamColor1Label);
        param->setDefault(1., 1., 1., 1. );
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

    ofxsMaskMixDescribeParams(desc, page);
} // RadialPluginFactory::describeInContext

static RadialPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
