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
 * OFX Threshold plugin.
 */

#include <cmath>
#include <cfloat> // DBL_MAX
#include <cstring>
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsCoords.h"
#include "ofxsMacros.h"
#ifdef OFX_EXTENSIONS_NATRON
#include "ofxNatron.h"
#endif
#include "ofxsThreadSuite.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "Threshold"
#define kPluginGrouping "Color"
#define kPluginDescription \
    "Threshold the selected channels, so that values less than the given Threshold Value become zero, and values greater than or equal become one.\n" \
    "If the Threshold Softness is nonzero, values less than value-softness become zero, values greater than value+softness become one, and values are linearly interpolated inbetween.\n" \
    "Note that when thresholding color values with a non-opaque alpha, the color values should in general be unpremultiplied for thresholding."

#define STRINGIZE_CPP_NAME_(token) # token
#define STRINGIZE_CPP_(token) STRINGIZE_CPP_NAME_(token)

#define kPluginIdentifier "net.sf.openfx.Threshold"

// History:
// version 1.0: initial version
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

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

#define kParamLevelName  "level"
#define kParamLevelLabel "Threshold Level"
#define kParamLevelHint  "Threshold level for the selected channels."

#define kParamSoftnessName  "softness"
#define kParamSoftnessLabel "Threshold Softness"
#define kParamSoftnessHint  "Threshold softness for the selected channels."

#ifdef OFX_EXTENSIONS_NATRON
#define OFX_COMPONENTS_OK(c) ((c)== ePixelComponentAlpha || (c) == ePixelComponentXY || (c) == ePixelComponentRGB || (c) == ePixelComponentRGBA)
#else
#define OFX_COMPONENTS_OK(c) ((c)== ePixelComponentAlpha || (c) == ePixelComponentRGB || (c) == ePixelComponentRGBA)
#endif


struct RGBAValues
{
    double r, g, b, a;
    RGBAValues(double v) : r(v), g(v), b(v), a(v) {}

    RGBAValues() : r(0), g(0), b(0), a(0) {}
};

class ThresholdProcessorBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg;
    bool _processR;
    bool _processG;
    bool _processB;
    bool _processA;
    RGBAValues _level;
    RGBAValues _softness;

public:

    ThresholdProcessorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _srcImg(NULL)
        , _processR(true)
        , _processG(true)
        , _processB(true)
        , _processA(false)
        , _level()
        , _softness()
    {
    }

    void setSrcImg(const Image *v) {_srcImg = v; }

    void setValues(bool processR,
                   bool processG,
                   bool processB,
                   bool processA,
                   const RGBAValues& level,
                   const RGBAValues& softness)
    {
        _processR = processR;
        _processG = processG;
        _processB = processB;
        _processA = processA;
        _level = level;
        _softness = softness;
    }

private:
};


template <class PIX, int nComponents, int maxValue>
class ThresholdProcessor
    : public ThresholdProcessorBase
{
public:
    ThresholdProcessor(ImageEffect &instance)
        : ThresholdProcessorBase(instance)
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

    PIX threshold(PIX value, double low, double high)
    {
        if (value <= low * maxValue) {
          return (PIX)0;
        }
        if (value >= high * maxValue) {
          return (PIX)maxValue;
        }
        return (PIX)((value - low * maxValue) / (high - low) + (maxValue == 1 ? 0. : 0.5));
    }
  
    template<bool processR, bool processG, bool processB, bool processA>
    void process(const OfxRectI& procWindow, const OfxPointD& rs)
    {
        unused(rs);
        assert(nComponents == 1 || nComponents == 3 || nComponents == 4);
        assert(_dstImg);
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                if (nComponents == 1) {
                    if (processA && srcPix) {
                       dstPix[0] = threshold(srcPix[0], _level.a - _softness.a, _level.a + _softness.a);
                    } else {
                       dstPix[0] = srcPix ? srcPix[0] : PIX();
                    }
                } else if ( (nComponents == 3) || (nComponents == 4) ) {
                    if ( processR && srcPix ) {
                        dstPix[0] = threshold(srcPix[0], _level.r - _softness.r, _level.r + _softness.r);
                    } else {
                        dstPix[0] = srcPix ? srcPix[0] : PIX();
                    }
                    if ( processG && srcPix ) {
                        dstPix[1] = threshold(srcPix[1], _level.g - _softness.g, _level.g + _softness.g);
                    } else {
                        dstPix[1] = srcPix ? srcPix[1] : PIX();
                    }
                    if ( processB && srcPix ) {
                        dstPix[2] = threshold(srcPix[2], _level.b - _softness.b, _level.b + _softness.b);
                    } else {
                        dstPix[2] = srcPix ? srcPix[2] : PIX();
                    }
                    if ( processA && srcPix && nComponents == 4 ) {
                        dstPix[3] = threshold(srcPix[3], _level.a - _softness.a, _level.a + _softness.a);
                    } else {
                        dstPix[3] = srcPix ? srcPix[3] : PIX();
                    }
                }
                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    } // process
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ThresholdPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    ThresholdPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _processR(NULL)
        , _processG(NULL)
        , _processB(NULL)
        , _processA(NULL)
        , _level(NULL)
        , _softness(NULL)
    {

        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || OFX_COMPONENTS_OK(_dstClip->getPixelComponents())) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || OFX_COMPONENTS_OK(_srcClip->getPixelComponents())) ) );
        _processR = fetchBooleanParam(kParamProcessR);
        _processG = fetchBooleanParam(kParamProcessG);
        _processB = fetchBooleanParam(kParamProcessB);
        _processA = fetchBooleanParam(kParamProcessA);
        assert(_processR && _processG && _processB && _processA);
        _level = fetchRGBAParam(kParamLevelName);
        _softness = fetchRGBAParam(kParamSoftnessName);
        assert(_level && _softness);
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    template <int nComponents>
    void renderInternal(const RenderArguments &args, BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(ThresholdProcessorBase &, const RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    BooleanParam* _processR;
    BooleanParam* _processG;
    BooleanParam* _processB;
    BooleanParam* _processA;
    RGBAParam *_level;
    RGBAParam *_softness;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
ThresholdPlugin::setupAndProcess(ThresholdProcessorBase &processor,
                           const RenderArguments &args)
{
    auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );

    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
# ifndef NDEBUG
    BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        throwSuiteStatusException(kOfxStatFailed);
    }
    checkBadRenderScaleOrField(dst, args);
# endif
    auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                    _srcClip->fetchImage(args.time) : 0 );
# ifndef NDEBUG
    if ( src.get() ) {
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
# endif
    // set the images
    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );
    // set the render window
    processor.setRenderWindow(args.renderWindow, args.renderScale);

    bool processR, processG, processB, processA;
    _processR->getValueAtTime(args.time, processR);
    _processG->getValueAtTime(args.time, processG);
    _processB->getValueAtTime(args.time, processB);
    _processA->getValueAtTime(args.time, processA);
    RGBAValues level;
    _level->getValueAtTime(args.time, level.r, level.g, level.b, level.a);
    RGBAValues softness;
    _softness->getValueAtTime(args.time, softness.r, softness.g, softness.b, softness.a);
    processor.setValues(processR, processG, processB, processA,
                        level, softness);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // ThresholdPlugin::setupAndProcess

// the internal render function
template <int nComponents>
void
ThresholdPlugin::renderInternal(const RenderArguments &args,
                          BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case eBitDepthUByte: {
        ThresholdProcessor<unsigned char, nComponents, 255> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthUShort: {
        ThresholdProcessor<unsigned short, nComponents, 65536> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthFloat: {
        ThresholdProcessor<float, nComponents, 1> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
ThresholdPlugin::render(const RenderArguments &args)
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
ThresholdPlugin::isIdentity(const IsIdentityArguments &args,
                            Clip * &identityClip,
                            double & /*identityTime*/,
                            int & /*identityView*/,
                            std::string& /*identityPlane*/)
{
    {
        bool processR, processG, processB, processA;
        _processR->getValueAtTime(args.time, processR);
        _processG->getValueAtTime(args.time, processG);
        _processB->getValueAtTime(args.time, processB);
        _processA->getValueAtTime(args.time, processA);
        if ( !processR && !processG && !processB && !processA ) {
            identityClip = _srcClip;

            return true;
        }
    }

    return false;
} // ThresholdPlugin::isIdentity

mDeclarePluginFactory(ThresholdPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
ThresholdPluginFactory::describe(ImageEffectDescriptor &desc)
{
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

#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone); // we have our own channel selector
#endif
}

void
ThresholdPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                          ContextEnum /*context*/)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

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
        if (page) {
            page->addChild(*param);
        }
    }

    {
        RGBAParamDescriptor *param = desc.defineRGBAParam(kParamLevelName);
        param->setLabel(kParamLevelLabel);
        param->setHint(kParamLevelHint);
        param->setDefault(0.0, 0.0, 0.0, 0.0);
        param->setRange(-DBL_MAX, -DBL_MAX, -DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(-1, -1, -1, -1, 1, 1, 1, 1);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }
    {
        RGBAParamDescriptor *param = desc.defineRGBAParam(kParamSoftnessName);
        param->setLabel(kParamSoftnessLabel);
        param->setHint(kParamSoftnessHint);
        param->setDefault(0.0, 0.0, 0.0, 0.0);
        param->setRange(0., 0., 0., 0., DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0, 0, 0, 0, 1, 1, 1, 1);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }
} // ThresholdPluginFactory::describeInContext

ImageEffect*
ThresholdPluginFactory::createInstance(OfxImageEffectHandle handle,
                                 ContextEnum /*context*/)
{
    return new ThresholdPlugin(handle);
}

static ThresholdPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
