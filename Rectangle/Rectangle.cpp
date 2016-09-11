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
 * OFX Rectangle plugin.
 */

// NOTE: This plugin is very similar to Radial. Any changes made here should probably be made in Radial.

#include <cmath>
#include <climits>
#include <cfloat> // DBL_MAX
#include <algorithm>

//#define DEBUG_HOSTDESCRIPTION
#ifdef DEBUG_HOSTDESCRIPTION
#include <iostream> // for host description printing code
#include "ofxOpenGLRender.h"
#endif

#include "ofxsProcessing.H"
#include "ofxsCoords.h"
#include "ofxsMaskMix.h"
#include "ofxsRectangleInteract.h"
#include "ofxsMacros.h"
#include "ofxsGenerator.h"
#ifdef OFX_EXTENSIONS_NATRON
#include "ofxNatron.h"
#endif

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#define POINT_TOLERANCE 6
#define POINT_SIZE 5

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "RectangleOFX"
#define kPluginGrouping "Draw"
#define kPluginDescription \
    "Draw a rectangle.\n" \
    "The rectangle is composited with the source image using the 'over' operator."
#define kPluginIdentifier "net.sf.openfx.Rectangle"
// History:
// version 1.0: initial version
// version 2.0: use kNatronOfxParamProcess* parameters
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsByte true
#define kSupportsUShort true
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
#define kParamSoftnessHint "Softness of the rectangle edges. 0 is a hard edge."

#define kParamColor0 "color0"
#define kParamColor0Label "Color 0"

#define kParamColor1 "color1"
#define kParamColor1Label "Color 1"

#define kParamExpandRoD "expandRoD"
#define kParamExpandRoDLabel "Expand RoD"
#define kParamExpandRoDHint "Expand the source region of definition by the shape RoD (if Source is connected and color0.a=0)."

#define kParamBlackOutside "blackOutside"
#define kParamBlackOutsideLabel "Black Outside"
#define kParamBlackOutsideHint "Add a 1 pixel black and transparent border if the plugin is used as a generator."


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

class RectangleProcessorBase
    : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImg;
    const OFX::Image *_maskImg;
    bool _doMasking;
    double _mix;
    bool _maskInvert;
    bool _processR;
    bool _processG;
    bool _processB;
    bool _processA;
    OfxPointD _btmLeft, _size;
    double _softness;
    RGBAValues _color0, _color1;

public:
    RectangleProcessorBase(OFX::ImageEffect &instance)
        : OFX::ImageProcessor(instance)
        , _srcImg(0)
        , _maskImg(0)
        , _doMasking(false)
        , _mix(1.)
        , _maskInvert(false)
        , _processR(false)
        , _processG(false)
        , _processB(false)
        , _processA(false)
        , _softness(0.)
    {
        _btmLeft.x = _btmLeft.y = _size.x = _size.y = 0.;
        _color0.r = _color0.g = _color0.b = _color0.a = 0.;
        _color1.r = _color1.g = _color1.b = _color1.a = 0.;
    }

    /** @brief set the src image */
    void setSrcImg(const OFX::Image *v)
    {
        _srcImg = v;
    }

    void setMaskImg(const OFX::Image *v,
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
        _softness = std::max(0., softness);
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
class RectangleProcessor
    : public RectangleProcessorBase
{
public:
    RectangleProcessor(OFX::ImageEffect &instance)
        : RectangleProcessorBase(instance)
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

        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                OfxPointI p_pixel;
                OfxPointD p;
                p_pixel.x = x;
                p_pixel.y = y;
                OFX::Coords::toCanonical(p_pixel, _dstImg->getRenderScale(), _dstImg->getPixelAspectRatio(), &p);
                double dx = std::min(p.x - _btmLeft.x, _btmLeft.x + _size.x - p.x);
                double dy = std::min(p.y - _btmLeft.y, _btmLeft.y + _size.y - p.y);

                if ( (dx <= 0) || (dy <= 0) ) {
                    // outside of the rectangle
                    tmpPix[0] = (float)_color0.r;
                    tmpPix[1] = (float)_color0.g;
                    tmpPix[2] = (float)_color0.b;
                    tmpPix[3] = (float)_color0.a;
                } else if ( (_softness == 0) || ( (dx >= _softness) && (dy >= _softness) ) ) {
                    // inside of the rectangle
                    tmpPix[0] = (float)_color1.r;
                    tmpPix[1] = (float)_color1.g;
                    tmpPix[2] = (float)_color1.b;
                    tmpPix[3] = (float)_color1.a;
                } else {
                    float tx, ty;
                    if (dx >= _softness) {
                        tx = 1.f;
                    } else {
                        tx = (float)rampSmooth(dx / _softness);
                    }
                    if (dy >= _softness) {
                        ty = 1.f;
                    } else {
                        ty = (float)rampSmooth(dy / _softness);
                    }
                    float t = tx * ty;
                    if (t >= 1) {
                        tmpPix[0] = (float)_color1.r;
                        tmpPix[1] = (float)_color1.g;
                        tmpPix[2] = (float)_color1.b;
                        tmpPix[3] = (float)_color1.a;
                    } else {
                        //if (_plinear) {
                        //    // it seems to be the way Nuke does it... I could understand t*t, but why t*t*t?
                        //    t = t*t*t;
                        //}
                        tmpPix[0] = (float)_color0.r * (1.f - t) + (float)_color1.r * t;
                        tmpPix[1] = (float)_color0.g * (1.f - t) + (float)_color1.g * t;
                        tmpPix[2] = (float)_color0.b * (1.f - t) + (float)_color1.b * t;
                        tmpPix[3] = (float)_color0.a * (1.f - t) + (float)_color1.a * t;
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
                    tmpPix[0] = tmpPix[0] + srcPixRGBA[0] * (1.f - (float)a);
                } else {
                    tmpPix[0] = srcPixRGBA[0];
                }
                if (processG) {
                    tmpPix[1] = tmpPix[1] + srcPixRGBA[1] * (1.f - (float)a);
                } else {
                    tmpPix[1] = srcPixRGBA[1];
                }
                if (processB) {
                    tmpPix[2] = tmpPix[2] + srcPixRGBA[2] * (1.f - (float)a);
                } else {
                    tmpPix[2] = srcPixRGBA[2];
                }
                if (processA) {
                    tmpPix[3] = tmpPix[3] + srcPixRGBA[3] * (1.f - (float)a);
                } else {
                    tmpPix[3] = srcPixRGBA[3];
                }
                if (nComponents == 1) {
                    tmpPix[0] = tmpPix[3];
                }
                ofxsMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
            }
        }
    } // process
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class RectanglePlugin
    : public GeneratorPlugin
{
public:
    /** @brief ctor */
    RectanglePlugin(OfxImageEffectHandle handle)
        : GeneratorPlugin(handle, false, kSupportsByte, kSupportsUShort, kSupportsFloat)
        , _srcClip(0)
        , _maskClip(0)
        , _processR(0)
        , _processG(0)
        , _processB(0)
        , _processA(0)
        , _color0(0)
        , _color1(0)
        , _expandRoD(0)
        , _blackOutside(0)
    {
        _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == OFX::eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentRGBA ||
                               _srcClip->getPixelComponents() == ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentXY ||
                               _srcClip->getPixelComponents() == ePixelComponentAlpha) ) );
        _maskClip = fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || !_maskClip->isConnected() || _maskClip->getPixelComponents() == ePixelComponentAlpha);

        _processR = fetchBooleanParam(kParamProcessR);
        _processG = fetchBooleanParam(kParamProcessG);
        _processB = fetchBooleanParam(kParamProcessB);
        _processA = fetchBooleanParam(kParamProcessA);
        assert(_processR && _processG && _processB && _processA);
        _softness = fetchDoubleParam(kParamSoftness);
        _color0 = fetchRGBAParam(kParamColor0);
        _color1 = fetchRGBAParam(kParamColor1);
        _expandRoD = fetchBooleanParam(kParamExpandRoD);
        _blackOutside = fetchBooleanParam(kParamBlackOutside);
        assert(_softness && _color0 && _color1 && _expandRoD && _blackOutside);

        _mix = fetchDoubleParam(kParamMix);
        _maskApply = paramExists(kParamMaskApply) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
    }

private:
    /* override is identity */
    virtual bool isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    /* Override the clip preferences */
    void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    template <int nComponents>
    void renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(RectangleProcessorBase &, const OFX::RenderArguments &args);

    virtual bool paramsNotAnimated() OVERRIDE FINAL;
    virtual OFX::Clip* getSrcClip() const OVERRIDE FINAL
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
    RGBAParam* _color0;
    RGBAParam* _color1;
    BooleanParam* _expandRoD;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskApply;
    OFX::BooleanParam* _maskInvert;
    OFX::BooleanParam* _blackOutside;
};

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
RectanglePlugin::setupAndProcess(RectangleProcessorBase &processor,
                                 const OFX::RenderArguments &args)
{
    std::auto_ptr<OFX::Image> dst( _dstClip->fetchImage(args.time) );

    if ( !dst.get() ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    const double time = args.time;
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
    if (doMasking) {
        if ( mask.get() ) {
            if ( (mask->getRenderScale().x != args.renderScale.x) ||
                 ( mask->getRenderScale().y != args.renderScale.y) ||
                 ( ( mask->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( mask->getField() != args.fieldToRender) ) ) {
                setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                OFX::throwSuiteStatusException(kOfxStatFailed);
            }
        }
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    if ( src.get() && dst.get() ) {
        OFX::BitDepthEnum srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
        OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();

        // see if they have the same depths and bytes and all
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
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
        bool wasCaught = GeneratorPlugin::getRegionOfDefinition(rod);
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
    double softness;
    _softness->getValueAtTime(args.time, softness);

    RGBAValues color0, color1;
    _color0->getValueAtTime(args.time, color0.r, color0.g, color0.b, color0.a);
    _color1->getValueAtTime(args.time, color1.r, color1.g, color1.b, color1.a);

    bool processR, processG, processB, processA;
    _processR->getValueAtTime(time, processR);
    _processG->getValueAtTime(time, processG);
    _processB->getValueAtTime(time, processB);
    _processA->getValueAtTime(time, processA);

    double mix;
    _mix->getValueAtTime(args.time, mix);

    processor.setValues(btmLeft, size,
                        softness,
                        color0, color1,
                        mix,
                        processR, processG, processB, processA);
    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // RectanglePlugin::setupAndProcess

// the internal render function
template <int nComponents>
void
RectanglePlugin::renderInternal(const OFX::RenderArguments &args,
                                OFX::BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case OFX::eBitDepthUByte: {
        RectangleProcessor<unsigned char, nComponents, 255> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case OFX::eBitDepthUShort: {
        RectangleProcessor<unsigned short, nComponents, 65535> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case OFX::eBitDepthFloat: {
        RectangleProcessor<float, nComponents, 1> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    default:
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
RectanglePlugin::render(const OFX::RenderArguments &args)
{
    assert( _dstClip && _dstClip->isConnected() );
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(dstComponents == OFX::ePixelComponentRGBA || dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentXY || dstComponents == OFX::ePixelComponentAlpha);
    if (dstComponents == OFX::ePixelComponentRGBA) {
        renderInternal<4>(args, dstBitDepth);
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        renderInternal<3>(args, dstBitDepth);
    } else if (dstComponents == OFX::ePixelComponentXY) {
        renderInternal<2>(args, dstBitDepth);
    } else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        renderInternal<1>(args, dstBitDepth);
    }
}

bool
RectanglePlugin::isIdentity(const OFX::IsIdentityArguments &args,
                            OFX::Clip * &identityClip,
                            double &identityTime)
{
    if ( GeneratorPlugin::isIdentity(args, identityClip, identityTime) ) {
        return true;
    }

    if (!_srcClip) {
        return false;
    }
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

    RGBAValues color0, color1;
    _color0->getValueAtTime(args.time, color0.r, color0.g, color0.b, color0.a);
    _color1->getValueAtTime(args.time, color1.r, color1.g, color1.b, color1.a);
    if ( (color0.a == 0.) && (color1.a == 0.) ) {
        identityClip = _srcClip;

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

    return false;
} // RectanglePlugin::isIdentity

bool
RectanglePlugin::paramsNotAnimated()
{
    return ( (!_processR || _processR->getNumKeys() == 0) &&
             (!_processG || _processG->getNumKeys() == 0) &&
             (!_processB || _processB->getNumKeys() == 0) &&
             (!_processA || _processA->getNumKeys() == 0) &&
             (!_softness || _softness->getNumKeys() == 0) &&
             (!_color0 || _color0->getNumKeys() == 0) &&
             (!_color1 || _color1->getNumKeys() == 0) &&
             (!_expandRoD || _expandRoD->getNumKeys() == 0) &&
             (!_mix || _mix->getNumKeys() == 0) &&
             (!_maskInvert || _maskInvert->getNumKeys() == 0) &&
             (!_blackOutside || _blackOutside->getNumKeys() == 0) );
}

/* Override the clip preferences */
void
RectanglePlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
    if (_srcClip) {
        // set the premultiplication of _dstClip if alpha is affected and source is Opaque
        bool processA;
        _processA->getValue(processA);
        if ( processA &&
             ( ( _dstClip->getPixelComponents() == ePixelComponentRGBA) ||
               ( _dstClip->getPixelComponents() == ePixelComponentAlpha) ) &&
             ( _srcClip->getPreMultiplication() == eImageOpaque) ) {
            clipPreferences.setOutputPremultiplication(eImageUnPreMultiplied);
        }
    }

    GeneratorPlugin::getClipPreferences(clipPreferences);
}

bool
RectanglePlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args,
                                       OfxRectD &rod)
{
    const double time = args.time;
    double mix;

    _mix->getValueAtTime(time, mix);
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
    if (color0.a != 0.) {
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
    if (color1.a == 0.) {
        if ( _srcClip && _srcClip->isConnected() ) {
            // nothing to draw: return default region of definition
            return false;
        } else {
            // empty RoD
            rod.x1 = rod.y1 = rod.x2 = rod.y2 = 0.;

            return true;
        }
    }
    bool expandRoD;
    _expandRoD->getValueAtTime(time, expandRoD);
    if (_srcClip && _srcClip->isConnected() && !expandRoD) {
        return false;
    }

    bool wasCaught = GeneratorPlugin::getRegionOfDefinition(rod);
    bool blackOutside;
    _blackOutside->getValueAtTime(time, blackOutside);
    rod.x1 -= (int)blackOutside;
    rod.y1 -= (int)blackOutside;
    rod.x2 += (int)blackOutside;
    rod.y2 += (int)blackOutside;

    if ( _srcClip && _srcClip->isConnected() ) {
        // something has to be drawn outside of the rectangle: return union of input RoD and rectangle
        const OfxRectD& srcRoD = _srcClip->getRegionOfDefinition(time);
        OFX::Coords::rectBoundingBox(rod, srcRoD, &rod);
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
} // RectanglePlugin::getRegionOfDefinition

mDeclarePluginFactory(RectanglePluginFactory, {}, {});
void
RectanglePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
#ifdef DEBUG_HOSTDESCRIPTION
    {
        const ImageEffectHostDescription& hostDesc = *OFX::getImageEffectHostDescription();
        std::cout << "OFX host description follows" << std::endl;
        std::cout << "OFX API version " << hostDesc.APIVersionMajor << '.' << hostDesc.APIVersionMinor << std::endl;
        std::cout << "hostName=" << hostDesc.hostName << std::endl;
        std::cout << "hostLabel=" << hostDesc.hostLabel << std::endl;
        std::cout << "hostVersion=" << hostDesc.versionMajor << '.' << hostDesc.versionMinor << '.' << hostDesc.versionMicro;
        std::cout << " (" << hostDesc.versionLabel << ')' << std::endl;
        std::cout << "hostIsBackground=" << hostDesc.hostIsBackground << std::endl;
        std::cout << "supportsOverlays=" << hostDesc.supportsOverlays << std::endl;
        std::cout << "supportsMultiResolution=" << hostDesc.supportsMultiResolution << std::endl;
        std::cout << "supportsTiles=" << hostDesc.supportsTiles << std::endl;
        std::cout << "temporalClipAccess=" << hostDesc.temporalClipAccess << std::endl;
        bool first;
        first = true;
        std::cout << "supportedComponents=";
        for (std::vector<PixelComponentEnum>::const_iterator it = hostDesc._supportedComponents.begin(); it != hostDesc._supportedComponents.end(); ++it) {
            if (!first) {
                std::cout << ",";
            }
            first = false;
            std::cout << mapPixelComponentEnumToStr(*it);
        }
        std::cout << std::endl;
        first = true;
        std::cout << "supportedContexts=";
        for (std::vector<ContextEnum>::const_iterator it = hostDesc._supportedContexts.begin(); it != hostDesc._supportedContexts.end(); ++it) {
            if (!first) {
                std::cout << ",";
            }
            first = false;
            std::cout << mapContextEnumToStr(*it);
        }
        std::cout << std::endl;
        first = true;
        std::cout << "supportedPixelDepths=";
        for (std::vector<BitDepthEnum>::const_iterator it = hostDesc._supportedPixelDepths.begin(); it != hostDesc._supportedPixelDepths.end(); ++it) {
            if (!first) {
                std::cout << ",";
            }
            first = false;
            std::cout << mapBitDepthEnumToStr(*it);
        }
        std::cout << std::endl;
        std::cout << "supportsMultipleClipDepths=" << hostDesc.supportsMultipleClipDepths << std::endl;
        std::cout << "supportsMultipleClipPARs=" << hostDesc.supportsMultipleClipPARs << std::endl;
        std::cout << "supportsSetableFrameRate=" << hostDesc.supportsSetableFrameRate << std::endl;
        std::cout << "supportsSetableFielding=" << hostDesc.supportsSetableFielding << std::endl;
        std::cout << "supportsStringAnimation=" << hostDesc.supportsStringAnimation << std::endl;
        std::cout << "supportsCustomInteract=" << hostDesc.supportsCustomInteract << std::endl;
        std::cout << "supportsChoiceAnimation=" << hostDesc.supportsChoiceAnimation << std::endl;
        std::cout << "supportsBooleanAnimation=" << hostDesc.supportsBooleanAnimation << std::endl;
        std::cout << "supportsCustomAnimation=" << hostDesc.supportsCustomAnimation << std::endl;
        std::cout << "supportsParametricAnimation=" << hostDesc.supportsParametricAnimation << std::endl;
#ifdef OFX_EXTENSIONS_NUKE
        std::cout << "canTransform=" << hostDesc.canTransform << std::endl;
#endif
        std::cout << "maxParameters=" << hostDesc.maxParameters << std::endl;
        std::cout << "pageRowCount=" << hostDesc.pageRowCount << std::endl;
        std::cout << "pageColumnCount=" << hostDesc.pageColumnCount << std::endl;
#ifdef OFX_EXTENSIONS_NATRON
        std::cout << "isNatron=" << hostDesc.isNatron << std::endl;
        std::cout << "supportsDynamicChoices=" << hostDesc.supportsDynamicChoices << std::endl;
        std::cout << "supportsCascadingChoices=" << hostDesc.supportsCascadingChoices << std::endl;
        std::cout << "supportsChannelSelector=" << hostDesc.supportsChannelSelector << std::endl;
#endif
        std::cout << "suites=";
        if ( fetchSuite(kOfxImageEffectSuite, 1, true) ) {
            std::cout << kOfxImageEffectSuite << ',';
        }
        if ( fetchSuite(kOfxPropertySuite, 1, true) ) {
            std::cout << kOfxPropertySuite << ',';
        }
        if ( fetchSuite(kOfxParameterSuite, 1, true) ) {
            std::cout << kOfxParameterSuite << ',';
        }
        if ( fetchSuite(kOfxMemorySuite, 1, true) ) {
            std::cout << kOfxMemorySuite << ',';
        }
        if ( fetchSuite(kOfxMultiThreadSuite, 1, true) ) {
            std::cout << kOfxMultiThreadSuite << ',';
        }
        if ( fetchSuite(kOfxMessageSuite, 1, true) ) {
            std::cout << kOfxMessageSuite << ',';
        }
        if ( fetchSuite(kOfxMessageSuite, 2, true) ) {
            std::cout << kOfxMessageSuite << "V2" << ',';
        }
        if ( fetchSuite(kOfxProgressSuite, 1, true) ) {
            std::cout << kOfxProgressSuite << ',';
        }
        if ( fetchSuite(kOfxTimeLineSuite, 1, true) ) {
            std::cout << kOfxTimeLineSuite << ',';
        }
        if ( fetchSuite(kOfxParametricParameterSuite, 1, true) ) {
            std::cout << kOfxParametricParameterSuite << ',';
        }
        if ( fetchSuite(kOfxOpenGLRenderSuite, 1, true) ) {
            std::cout << kOfxOpenGLRenderSuite << ',';
        }
#ifdef OFX_EXTENSIONS_NUKE
        if ( fetchSuite(kNukeOfxCameraSuite, 1, true) ) {
            std::cout << kNukeOfxCameraSuite << ',';
        }
        if ( fetchSuite(kFnOfxImageEffectPlaneSuite, 1, true) ) {
            std::cout << kFnOfxImageEffectPlaneSuite << "V1" << ',';
        }
        if ( fetchSuite(kFnOfxImageEffectPlaneSuite, 2, true) ) {
            std::cout << kFnOfxImageEffectPlaneSuite << "V2" << ',';
        }
#endif
#ifdef OFX_EXTENSIONS_VEGAS
        if ( fetchSuite(kOfxVegasProgressSuite, 1, true) ) {
            std::cout << kOfxVegasProgressSuite << ',';
        }
        if ( fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true) ) {
            std::cout << kOfxVegasStereoscopicImageEffectSuite << ',';
        }
        if ( fetchSuite(kOfxVegasKeyframeSuite, 1, true) ) {
            std::cout << kOfxVegasKeyframeSuite << ',';
        }
#endif
        if ( fetchSuite("OfxOpenCLProgramSuite", 1, true) ) {
            std::cout << "OfxOpenCLProgramSuite" << ',';
        }
        std::cout << std::endl;
        std::cout << "OFX DebugProxy: host description finished" << std::endl;
    }
#endif // ifdef DEBUG_HOSTDESCRIPTION
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
    // in order to support multiresolution, render() must take into account the pixelaspectratio and the renderscale
    // and scale the transform appropriately.
    // All other functions are usually in canonical coordinates.
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderTwiceAlways(false);
    desc.setRenderThreadSafety(kRenderThreadSafety);

    generatorDescribe(desc);

#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(OFX::ePixelComponentNone); // we have our own channel selector
#endif
} // RectanglePluginFactory::describe

OFX::ImageEffect*
RectanglePluginFactory::createInstance(OfxImageEffectHandle handle,
                                       OFX::ContextEnum /*context*/)
{
    return new RectanglePlugin(handle);
}

void
RectanglePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                          OFX::ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    // always declare the source clip first, because some hosts may consider
    // it as the default input clip (e.g. Nuke)
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    //srcClip->addSupportedComponent(ePixelComponentRGB);
    //srcClip->addSupportedComponent(ePixelComponentXY);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    //srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    //srcClip->setIsMask(false);
    srcClip->setOptional(true);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    //dstClip->addSupportedComponent(ePixelComponentRGB);
    //dstClip->addSupportedComponent(ePixelComponentXY);
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
        param->setDefault(0.);
        param->setIncrement(0.01);
        param->setRange(0., DBL_MAX);
        param->setDisplayRange(0., 100.);
        param->setDigits(2);
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
        OFX::BooleanParamDescriptor *param = desc.defineBooleanParam(kParamExpandRoD);
        param->setLabel(kParamExpandRoDLabel);
        param->setHint(kParamExpandRoDHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // blackOutside
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamBlackOutside);
        param->setLabel(kParamBlackOutsideLabel);
        param->setDefault(true);
        param->setAnimates(true);
        param->setHint(kParamBlackOutsideHint);
        if (page) {
            page->addChild(*param);
        }
    }

    ofxsMaskMixDescribeParams(desc, page);
} // RectanglePluginFactory::describeInContext

static RectanglePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
