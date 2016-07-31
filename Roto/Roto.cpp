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
 * OFX Roto plugin.
 */

#include <cmath>
#include <algorithm>

#include "ofxsProcessing.H"
#include "ofxsCoords.h"
#include "ofxsMerging.h"
#include "ofxsMacros.h"

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

#define kParamPremult "premultiply"
#define kParamPremultLabel "Premultiply"
#define kParamPremultHint "Premultiply the red, green and blue channels with the alpha channel produced by the mask."


class RotoProcessorBase
    : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImg;
    const OFX::Image *_roto;
    bool _processR;
    bool _processG;
    bool _processB;
    bool _processA;

public:
    RotoProcessorBase(OFX::ImageEffect &instance)
        : OFX::ImageProcessor(instance)
        , _srcImg(0)
        , _roto(0)
        , _processR(false)
        , _processG(false)
        , _processB(false)
        , _processA(false)
    {
    }

    /** @brief set the src image */
    void setSrcImg(const OFX::Image *v)
    {
        _srcImg = v;
    }

    /** @brief set the optional mask image */
    void setRotoImg(const OFX::Image *v) {_roto = v; }

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
    RotoProcessor(OFX::ImageEffect &instance)
        : RotoProcessorBase(instance)
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

    template<bool processR, bool processG, bool processB, bool processA>
    void process(const OfxRectI& procWindow)
    {
        // roto and dst should have the same number of components
        assert( !_roto ||
                (_roto->getPixelComponents() == ePixelComponentAlpha && nComponents == 1) ||
                (_roto->getPixelComponents() == ePixelComponentXY && nComponents == 2) ||
                (_roto->getPixelComponents() == ePixelComponentRGB && nComponents == 3) ||
                (_roto->getPixelComponents() == ePixelComponentRGBA && nComponents == 4) );
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
                    dstPix[c] = OFX::MergeImages2D::overFunctor<PIX, maxValue>(maskPix ? maskPix[c] : PIX(), srcVal[c], maskAlpha, srcAlpha);
#                 ifdef DEBUG
                    assert(srcVal[c] == srcVal[c]); // check for NaN
                    assert(dstPix[c] == dstPix[c]); // check for NaN
#                 endif
                }
            }
        }
    } // process
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class RotoPlugin
    : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    RotoPlugin(OfxImageEffectHandle handle,
               bool /*masked*/)
        : ImageEffect(handle)
        , _dstClip(0)
        , _srcClip(0)
        , _rotoClip(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentAlpha ||
                             _dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == OFX::eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentAlpha ||
                               _srcClip->getPixelComponents() == ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA) ) );
        // name of mask clip depends on the context
        _rotoClip = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Roto");
        assert( _rotoClip && (_rotoClip->getPixelComponents() == ePixelComponentAlpha || _rotoClip->getPixelComponents() == ePixelComponentRGBA) );
        _processR = fetchBooleanParam(kNatronOfxParamProcessR);
        _processG = fetchBooleanParam(kNatronOfxParamProcessG);
        _processB = fetchBooleanParam(kNatronOfxParamProcessB);
        _processA = fetchBooleanParam(kNatronOfxParamProcessA);
        assert(_processR && _processG && _processB && _processA);
    }

private:
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /** @brief get the clip preferences */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    template <int nComponents>
    void renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(RotoProcessorBase &, const OFX::RenderArguments &args);

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;
    OFX::Clip *_rotoClip;
    OFX::BooleanParam* _processR;
    OFX::BooleanParam* _processG;
    OFX::BooleanParam* _processB;
    OFX::BooleanParam* _processA;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
RotoPlugin::setupAndProcess(RotoProcessorBase &processor,
                            const OFX::RenderArguments &args)
{
    std::auto_ptr<OFX::Image> dst( _dstClip->fetchImage(args.time) );

    if ( !dst.get() ) {
        setPersistentMessage(OFX::Message::eMessageError, "", "Could not fetch output image");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    const double time = args.time;
    OFX::BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }
    std::auto_ptr<const OFX::Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                         _srcClip->fetchImage(args.time) : 0 );
    if ( src.get() && dst.get() ) {
        if ( (src->getRenderScale().x != args.renderScale.x) ||
             ( src->getRenderScale().y != args.renderScale.y) ||
             ( ( src->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( src->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatErrFormat);
        }
        OFX::BitDepthEnum srcBitDepth = src->getPixelDepth();
        if (srcBitDepth != dstBitDepth) {
            OFX::throwSuiteStatusException(kOfxStatErrFormat);
        }
    }

    // auto ptr for the mask.
    std::auto_ptr<const OFX::Image> mask( ( _rotoClip && _rotoClip->isConnected() ) ?
                                          _rotoClip->fetchImage(args.time) : 0 );

    // do we do masking
    if ( _rotoClip && _rotoClip->isConnected() ) {
        if ( !mask.get() ) {
            setPersistentMessage(OFX::Message::eMessageError, "", "Error while rendering the roto mask");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        if ( mask.get() ) {
            if ( (mask->getRenderScale().x != args.renderScale.x) ||
                 ( mask->getRenderScale().y != args.renderScale.y) ||
                 ( ( mask->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( mask->getField() != args.fieldToRender) ) ) {
                setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                OFX::throwSuiteStatusException(kOfxStatFailed);
            }
        }
        assert(mask->getPixelComponents() == OFX::ePixelComponentRGBA || mask->getPixelComponents() == OFX::ePixelComponentAlpha
               || mask->getPixelComponents() == OFX::ePixelComponentRGB || mask->getPixelComponents() == OFX::ePixelComponentXY);
        if ( mask->getPixelComponents() != dst->getPixelComponents() ) {
            OFX::throwSuiteStatusException(kOfxStatErrFormat);
        }
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
    processor.setRenderWindow(args.renderWindow);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // RotoPlugin::setupAndProcess

// (see comments in Natron code about this feature being buggy)
bool
RotoPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args,
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

        OFX::Coords::rectBoundingBox(rod, rotoRod, &rod);

        return true;
    }
#endif
}

// the internal render function
template <int nComponents>
void
RotoPlugin::renderInternal(const OFX::RenderArguments &args,
                           OFX::BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case OFX::eBitDepthUByte: {
        RotoProcessor<unsigned char, nComponents, 255> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case OFX::eBitDepthUShort: {
        RotoProcessor<unsigned short, nComponents, 65535> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case OFX::eBitDepthFloat: {
        RotoProcessor<float, nComponents, 1> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    default:
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
RotoPlugin::render(const OFX::RenderArguments &args)
{
    assert(_srcClip && _dstClip);
    if (!_srcClip || !_dstClip) {
        throwSuiteStatusException(kOfxStatErrBadHandle);
    }
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(dstComponents == OFX::ePixelComponentRGBA || dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentAlpha || dstComponents == OFX::ePixelComponentXY);
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
RotoPlugin::isIdentity(const IsIdentityArguments &args,
                       Clip * &identityClip,
                       double & /*identityTime*/)
{
    if (!_srcClip) {
        return false;
    }
    const double time = args.time;
    OFX::PixelComponentEnum srcComponents  = _srcClip->getPixelComponents();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();
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
        OFX::Coords::toPixelEnclosing(_rotoClip->getRegionOfDefinition(args.time), args.renderScale, _rotoClip->getPixelAspectRatio(), &rotoRoD);
        // effect is identity if the renderWindow doesn't intersect the roto RoD
        if ( !OFX::Coords::rectIntersection<OfxRectI>(args.renderWindow, rotoRoD, 0) ) {
            identityClip = _srcClip;

            return true;
        }
    }

    return false;
}

void
RotoPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    if (!_srcClip) {
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

mDeclarePluginFactory(RotoPluginFactory, {}, {});
void
RotoPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
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
#endif
}

OFX::ImageEffect*
RotoPluginFactory::createInstance(OfxImageEffectHandle handle,
                                  OFX::ContextEnum /*context*/)
{
    return new RotoPlugin(handle, false);
}

void
RotoPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                     OFX::ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    // always declare the source clip first, because some hosts may consider
    // it as the default input clip (e.g. Nuke)
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    //srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentXY);
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
            maskClip->addSupportedComponent(ePixelComponentXY);
            maskClip->setOptional(false);
        }
        maskClip->setSupportsTiles(kSupportsTiles);
        maskClip->setIsMask(context == eContextPaint); // we are a mask input
    }

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    //dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentXY);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kNatronOfxParamProcessR);
        param->setLabel(kNatronOfxParamProcessRLabel);
        param->setHint(kNatronOfxParamProcessRHint);
        param->setDefault(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kNatronOfxParamProcessG);
        param->setLabel(kNatronOfxParamProcessGLabel);
        param->setHint(kNatronOfxParamProcessGHint);
        param->setDefault(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kNatronOfxParamProcessB);
        param->setLabel(kNatronOfxParamProcessBLabel);
        param->setHint(kNatronOfxParamProcessBHint);
        param->setDefault(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kNatronOfxParamProcessA);
        param->setLabel(kNatronOfxParamProcessALabel);
        param->setHint(kNatronOfxParamProcessAHint);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }
} // RotoPluginFactory::describeInContext

/*static RotoPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)*/

OFXS_NAMESPACE_ANONYMOUS_EXIT
