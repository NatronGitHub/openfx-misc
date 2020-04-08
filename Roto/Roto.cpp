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
 * OFX Roto plugin.
 * This plugin was used internally by Natron until Natron 2.0, and is now deprecated.
 */

#include <cmath>
#include <algorithm>

#include "ofxsProcessing.H"
#include "ofxsCoords.h"
#include "ofxsMerging.h"
#include "ofxsMacros.h"
#include "ofxsThreadSuite.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "RotoOFX"
#define kPluginGrouping "Draw"
#define kPluginDescription "Create masks and shapes."
#define kPluginIdentifier "net.sf.openfx.RotoPlugin"
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

#define kParamPremult "premultiply"
#define kParamPremultLabel "Premultiply"
#define kParamPremultHint "Premultiply the red, green and blue channels with the alpha channel produced by the mask."

#ifdef OFX_EXTENSIONS_NATRON
#define OFX_COMPONENTS_OK(c) ((c)== ePixelComponentAlpha || (c) == ePixelComponentXY || (c) == ePixelComponentRGB || (c) == ePixelComponentRGBA)
#else
#define OFX_COMPONENTS_OK(c) ((c)== ePixelComponentAlpha || (c) == ePixelComponentRGB || (c) == ePixelComponentRGBA)
#endif


class RotoProcessorBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg;
    const Image *_roto;
    bool _processR;
    bool _processG;
    bool _processB;
    bool _processA;

public:
    RotoProcessorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _srcImg(NULL)
        , _roto(NULL)
        , _processR(false)
        , _processG(false)
        , _processB(false)
        , _processA(false)
    {
    }

    /** @brief set the src image */
    void setSrcImg(const Image *v)
    {
        _srcImg = v;
    }

    /** @brief set the optional mask image */
    void setRotoImg(const Image *v) {_roto = v; }

    void setValues(bool processR,
                   bool processG,
                   bool processB,
                   bool processA)
    {
        _processR = processR;
        _processG = processG;
        _processB = processB;
        _processA = processA;
    }
};


// The "masked", "filter" and "clamp" template parameters allow filter-specific optimization
// by the compiler, using the same generic code for all filters.
template <class PIX, int nComponents, int maxValue>
class RotoProcessor
    : public RotoProcessorBase
{
public:
    RotoProcessor(ImageEffect &instance)
        : RotoProcessorBase(instance)
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

    template<bool processR, bool processG, bool processB, bool processA>
    void process(const OfxRectI& procWindow, const OfxPointD& rs)
    {
        unused(rs);
        // roto and dst should have the same number of components
#ifdef OFX_EXTENSIONS_NATRON
        assert( !_roto ||
                (_roto->getPixelComponents() == ePixelComponentAlpha && nComponents == 1) ||
                (_roto->getPixelComponents() == ePixelComponentXY && nComponents == 2) ||
                (_roto->getPixelComponents() == ePixelComponentRGB && nComponents == 3) ||
                (_roto->getPixelComponents() == ePixelComponentRGBA && nComponents == 4) );
#else
        assert( !_roto ||
               (_roto->getPixelComponents() == ePixelComponentAlpha && nComponents == 1) ||
               (_roto->getPixelComponents() == ePixelComponentRGB && nComponents == 3) ||
               (_roto->getPixelComponents() == ePixelComponentRGBA && nComponents == 4) );
#endif
        //assert(filter == _filter);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                const PIX *srcPix = (const PIX*)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                const PIX *maskPix = (const PIX*) (_roto ? _roto->getPixelAddress(x, y) : 0);
                PIX srcAlpha = PIX();
                if (srcPix) {
                    if (nComponents == 1) {
                        srcAlpha = srcPix[0];
                    } else if (nComponents == 4) {
                        srcAlpha = srcPix[3];
                    }
                }
                PIX maskAlpha;
                if (nComponents == 1) {
                    maskAlpha = maskPix ? maskPix[0] : 0;
                } else if (nComponents == 4) {
                    maskAlpha = maskPix ? maskPix[nComponents - 1] : 0;
                } else {
                    maskAlpha = 1;
                }
#             ifdef DEBUG
                assert( !OFX::IsNaN(srcAlpha) ); // check for NaN
                assert( !OFX::IsNaN(maskAlpha) ); // check for NaN
#             endif


                PIX srcVal[nComponents];
                // fill srcVal (hopefully the compiler will optimize this)
                if (!srcPix) {
                    for (int c = 0; c < nComponents; ++c) {
                        srcVal[c] = 0;
                    }
                } else if (nComponents == 1) {
                    srcVal[0] = srcAlpha;
                } else {
                    for (int c = 0; c < nComponents; ++c) {
                        srcVal[c] = srcPix[c];
                    }
                }

                // merge/over
                for (int c = 0; c < nComponents; ++c) {
                    dstPix[c] = MergeImages2D::overFunc<PIX, maxValue>(maskPix ? maskPix[c] : PIX(), srcVal[c], maskAlpha, srcAlpha);
#                 ifdef DEBUG
                    assert( !OFX::IsNaN(srcVal[c]) ); // check for NaN
                    assert( !OFX::IsNaN(dstPix[c]) ); // check for NaN
#                 endif
                }
            }
        }
    } // process
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class RotoPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    RotoPlugin(OfxImageEffectHandle handle,
               bool /*masked*/)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _rotoClip(NULL)
    {

        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentAlpha ||
                             _dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentAlpha ||
                               _srcClip->getPixelComponents() == ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA) ) );
        // name of mask clip depends on the context
        _rotoClip = getContext() == eContextFilter ? NULL : fetchClip(getContext() == eContextPaint ? "Brush" : "Roto");
        assert( _rotoClip && (_rotoClip->getPixelComponents() == ePixelComponentAlpha || _rotoClip->getPixelComponents() == ePixelComponentRGBA) );
        _processR = fetchBooleanParam(kParamProcessR);
        _processG = fetchBooleanParam(kParamProcessG);
        _processB = fetchBooleanParam(kParamProcessB);
        _processA = fetchBooleanParam(kParamProcessA);
        assert(_processR && _processG && _processB && _processA);
    }

private:
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /** @brief get the clip preferences */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;

    template <int nComponents>
    void renderInternal(const RenderArguments &args, BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(RotoProcessorBase &, const RenderArguments &args);

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    Clip *_rotoClip;
    BooleanParam* _processR;
    BooleanParam* _processG;
    BooleanParam* _processB;
    BooleanParam* _processA;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
RotoPlugin::setupAndProcess(RotoProcessorBase &processor,
                            const RenderArguments &args)
{
    auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );

    if ( !dst.get() ) {
        setPersistentMessage(Message::eMessageError, "", "Could not fetch output image");
        throwSuiteStatusException(kOfxStatFailed);
    }
    const double time = args.time;
# ifndef NDEBUG
    BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        throwSuiteStatusException(kOfxStatErrFormat);
    }
    checkBadRenderScaleOrField(dst, args);
# endif
    auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                    _srcClip->fetchImage(args.time) : 0 );
    if ( src.get() && dst.get() ) {
#     ifndef NDEBUG
        checkBadRenderScaleOrField(src, args);
        BitDepthEnum srcBitDepth = src->getPixelDepth();
        if (srcBitDepth != dstBitDepth) {
            throwSuiteStatusException(kOfxStatErrFormat);
        }
#     endif
    }

    // auto ptr for the mask.
    auto_ptr<const Image> mask( ( _rotoClip && _rotoClip->isConnected() ) ?
                                     _rotoClip->fetchImage(args.time) : 0 );

    // do we do masking
    if ( _rotoClip && _rotoClip->isConnected() ) {
        if ( !mask.get() ) {
            setPersistentMessage(Message::eMessageError, "", "Error while rendering the roto mask");
            throwSuiteStatusException(kOfxStatFailed);
        }
#     ifndef NDEBUG
        checkBadRenderScaleOrField(mask, args);
        assert(OFX_COMPONENTS_OK(mask->getPixelComponents()));
        if ( mask->getPixelComponents() != dst->getPixelComponents() ) {
            throwSuiteStatusException(kOfxStatErrFormat);
        }
#     endif
        // Set it in the processor
        processor.setRotoImg( mask.get() );
    }

    bool processR, processG, processB, processA;
    _processR->getValueAtTime(time, processR);
    _processG->getValueAtTime(time, processG);
    _processB->getValueAtTime(time, processB);
    _processA->getValueAtTime(time, processA);
    processor.setValues(processR, processG, processB, processA);

    // set the images
    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );

    // set the render window
    processor.setRenderWindow(args.renderWindow, args.renderScale);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // RotoPlugin::setupAndProcess

// (see comments in Natron code about this feature being buggy)
bool
RotoPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                  OfxRectD &rod)
{
#ifdef NATRON_ROTO_INVERTIBLE
    // if NATRON_ROTO_INVERTIBLE is defined (but this is buggy anyway),
    // RoD should be union(defaultRoD,inputsRoD)
    // Natron does this if the RoD is infinite
    rod.x1 = rod.y1 = kOfxFlagInfiniteMin;
    rod.x2 = rod.y2 = kOfxFlagInfiniteMax;

    return true;
#else
    // if source is not connected, use the Mask RoD (i.e. the default RoD)
    // else use the union of Source and Mask RoD (Source is optional)
    if ( !( _srcClip && _srcClip->isConnected() ) ) {
        return false;
    } else {
        rod = _srcClip->getRegionOfDefinition(args.time);
        OfxRectD rotoRod;
        try {
            rotoRod = _rotoClip->getRegionOfDefinition(args.time);
        } catch (...) {
            ///If an exception is thrown, that is because the RoD of the roto is NULL (i.e there isn't any shape)
            ///Don't fail getRegionOfDefinition, just take the RoD of the source instead so that in RGBA mode is still displays the source

            ///image
            return true;
        }

        Coords::rectBoundingBox(rod, rotoRod, &rod);

        return true;
    }
#endif
}

// the internal render function
template <int nComponents>
void
RotoPlugin::renderInternal(const RenderArguments &args,
                           BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case eBitDepthUByte: {
        RotoProcessor<unsigned char, nComponents, 255> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthUShort: {
        RotoProcessor<unsigned short, nComponents, 65535> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthFloat: {
        RotoProcessor<float, nComponents, 1> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
RotoPlugin::render(const RenderArguments &args)
{
    assert(_srcClip && _dstClip);
    if (!_srcClip || !_dstClip) {
        throwSuiteStatusException(kOfxStatErrBadHandle);
    }
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
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
RotoPlugin::isIdentity(const IsIdentityArguments &args,
                       Clip * &identityClip,
                       double & /*identityTime*/
                       , int& /*view*/, std::string& /*plane*/)
{
    if (!_srcClip || !_srcClip->isConnected()) {
        return false;
    }
    const double time = args.time;
    PixelComponentEnum srcComponents  = _srcClip->getPixelComponents();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();
    if (srcComponents != dstComponents) {
        return false;
    }

    bool processA;
    _processA->getValueAtTime(time, processA);

    if ( (srcComponents == ePixelComponentAlpha) && !processA ) {
        identityClip = _srcClip;

        return true;
    }
    bool processR, processG, processB;
    _processR->getValueAtTime(time, processR);
    _processG->getValueAtTime(time, processG);
    _processB->getValueAtTime(time, processB);
    if ( (srcComponents == ePixelComponentRGBA) && !processR && !processG && !processB && !processA ) {
        identityClip = _srcClip;

        return true;
    }

    if ( _rotoClip && _rotoClip->isConnected() ) {
        OfxRectI rotoRoD;
        Coords::toPixelEnclosing(_rotoClip->getRegionOfDefinition(args.time), args.renderScale, _rotoClip->getPixelAspectRatio(), &rotoRoD);
        // effect is identity if the renderWindow doesn't intersect the roto RoD
        if ( !Coords::rectIntersection<OfxRectI>(args.renderWindow, rotoRoD, 0) ) {
            identityClip = _srcClip;

            return true;
        }
    }

    return false;
}

void
RotoPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    if (!_srcClip || !_srcClip->isConnected()) {
        return;
    }
    PreMultiplicationEnum srcPremult = _srcClip->getPreMultiplication();
    bool processA;
    _processA->getValue(processA);
    if ( (srcPremult == eImageOpaque) && processA ) {
        // we're changing alpha, the image becomes UnPremultiplied
        clipPreferences.setOutputPremultiplication(eImageUnPreMultiplied);
    }
}

mDeclarePluginFactory(RotoPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
RotoPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextPaint);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);


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
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
    if (getImageEffectHostDescription()->isNatron) {
        desc.setIsDeprecated(true); // prefer Natron's internal Roto
    }
#endif
}

ImageEffect*
RotoPluginFactory::createInstance(OfxImageEffectHandle handle,
                                  ContextEnum /*context*/)
{
    return new RotoPlugin(handle, false);
}

void
RotoPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                     ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    // always declare the source clip first, because some hosts may consider
    // it as the default input clip (e.g. Nuke)
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    //srcClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    srcClip->addSupportedComponent(ePixelComponentXY);
#endif
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);
    srcClip->setOptional(true);

    // if general or paint context, define the mask clip
    if ( (context == eContextGeneral) || (context == eContextPaint) ) {
        // if paint context, it is a mandated input called 'brush'
        ClipDescriptor *maskClip = context == eContextGeneral ? desc.defineClip("Roto") : desc.defineClip("Brush");
        maskClip->setTemporalClipAccess(false);
        maskClip->addSupportedComponent(ePixelComponentAlpha);
        if (context != eContextPaint) {
            maskClip->addSupportedComponent(ePixelComponentRGBA);
            //maskClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
            maskClip->addSupportedComponent(ePixelComponentXY);
#endif
            maskClip->setOptional(false);
        }
        maskClip->setSupportsTiles(kSupportsTiles);
        maskClip->setIsMask(context == eContextPaint); // we are a mask input
    }

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    //dstClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    dstClip->addSupportedComponent(ePixelComponentXY);
#endif
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessR);
        param->setLabel(kParamProcessRLabel);
        param->setHint(kParamProcessRHint);
        param->setDefault(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessG);
        param->setLabel(kParamProcessGLabel);
        param->setHint(kParamProcessGHint);
        param->setDefault(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessB);
        param->setLabel(kParamProcessBLabel);
        param->setHint(kParamProcessBHint);
        param->setDefault(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessA);
        param->setLabel(kParamProcessALabel);
        param->setHint(kParamProcessAHint);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }
} // RotoPluginFactory::describeInContext

static RotoPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
