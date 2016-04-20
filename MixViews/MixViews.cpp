/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2015 INRIA
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
 * OFX MixViews plugin.
 * Mix two views together.
 */

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMacros.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "MixViewsOFX"
#define kPluginGrouping "Views/Stereo"
#define kPluginDescription "Mix two views together."
#define kPluginIdentifier "net.sf.openfx.mixViewsPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamMix "mix"
#define kParamMixLabel "Mix"
#define kParamMixHint "Mix factor for the right view"

// Base class for the RGBA and the Alpha processor
class MixViewsBase : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcLeftImg;
    const OFX::Image *_srcRightImg;
    float _mix;

public:
    /** @brief no arg ctor */
    MixViewsBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcLeftImg(0)
    , _srcRightImg(0)
    , _mix(0)
    {
    }

    /** @brief set the left src image */
    void setSrcLeftImg(const OFX::Image *v) {_srcLeftImg = v;}

    /** @brief set the right src image */
    void setSrcRightImg(const OFX::Image *v) {_srcRightImg = v;}

    /** @brief set the mix factor of right view */
    void setMix(float v) {_mix = v;}
};

// template to do the RGBA processing
template <class PIX, int nComponents, int max>
class ViewMixer : public MixViewsBase
{
public:
    // ctor
    ViewMixer(OFX::ImageEffect &instance)
    : MixViewsBase(instance)
    {
    }

private:
    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if (_effect.abort()) {
                break;
            }
            
            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcLeftPix = (const PIX *)(_srcLeftImg ? _srcLeftImg->getPixelAddress(x, y) : 0);
                const PIX *srcRightPix = (const PIX *)(_srcRightImg ? _srcRightImg->getPixelAddress(x, y) : 0);

                for (int c = 0; c < nComponents; c++) {
                    dstPix[c] = (srcLeftPix ? srcLeftPix[c] : PIX())*(1-_mix) + (srcRightPix ? srcRightPix[c] : PIX())*_mix;
                }

                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    }
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class MixViewsPlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    MixViewsPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    , _mix(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == ePixelComponentAlpha ||
                            _dstClip->getPixelComponents() == ePixelComponentRGB ||
                            _dstClip->getPixelComponents() == ePixelComponentRGBA));
        _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert((!_srcClip && getContext() == OFX::eContextGenerator) ||
               (_srcClip && (_srcClip->getPixelComponents() == ePixelComponentAlpha ||
                             _srcClip->getPixelComponents() == ePixelComponentRGB ||
                             _srcClip->getPixelComponents() == ePixelComponentRGBA)));
        _mix  = fetchDoubleParam(kParamMix);
        assert(_mix);
    }

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    template <int nComponents>
    void renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(MixViewsBase &, const OFX::RenderArguments &args);

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;
    OFX::DoubleParam *_mix;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


/* set up and run a processor */
void
MixViewsPlugin::setupAndProcess(MixViewsBase &processor, const OFX::RenderArguments &args)
{
    // get a dst image
    std::auto_ptr<OFX::Image> dst(_dstClip->fetchImage(args.time));
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum         dstBitDepth    = dst->getPixelDepth();
    OFX::PixelComponentEnum   dstComponents  = dst->getPixelComponents();
    if (dstBitDepth != _dstClip->getPixelDepth() ||
        dstComponents != _dstClip->getPixelComponents()) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dst->getRenderScale().x != args.renderScale.x ||
        dst->getRenderScale().y != args.renderScale.y ||
        (dst->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && dst->getField() != args.fieldToRender)) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    // fetch main input image
    std::auto_ptr<const OFX::Image> srcLeft((_srcClip && _srcClip->isConnected()) ?
                                            _srcClip->fetchStereoscopicImage(args.time, 0) : 0);
    std::auto_ptr<const OFX::Image> srcRight((_srcClip && _srcClip->isConnected()) ?
                                             _srcClip->fetchStereoscopicImage(args.time,1) : 0);

    // make sure bit depths are sane
    if (srcLeft.get()) {
        if (srcLeft->getRenderScale().x != args.renderScale.x ||
            srcLeft->getRenderScale().y != args.renderScale.y ||
            (srcLeft->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && srcLeft->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum    srcBitDepth      = srcLeft->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = srcLeft->getPixelComponents();

        // see if they have the same depths and bytes and all
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
    }
    if (srcRight.get()) {
        if (srcRight->getRenderScale().x != args.renderScale.x ||
            srcRight->getRenderScale().y != args.renderScale.y ||
            (srcRight->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && srcRight->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum    srcBitDepth      = srcRight->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = srcRight->getPixelComponents();

        // see if they have the same depths and bytes and all
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
    }

    double mix = _mix->getValueAtTime(args.time);

    // set the images
    processor.setDstImg(dst.get());
    processor.setSrcLeftImg(srcLeft.get());
    processor.setSrcRightImg(srcRight.get());

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    // set the parameters
    processor.setMix((float)mix);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

// the internal render function
template <int nComponents>
void
MixViewsPlugin::renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
        case OFX::eBitDepthUByte: {
            ViewMixer<unsigned char, nComponents, 255> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case OFX::eBitDepthUShort: {
            ViewMixer<unsigned short, nComponents, 65535> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case OFX::eBitDepthFloat: {
            ViewMixer<float, nComponents, 1> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        default:
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
MixViewsPlugin::render(const OFX::RenderArguments &args)
{
    if (!OFX::fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
        OFX::throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
    }

    assert(kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth());
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    // do the rendering
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


mDeclarePluginFactory(MixViewsPluginFactory, ;, {});

void MixViewsPluginFactory::load()
{
    // we can't be used on hosts that don't support the stereoscopic suite
    // returning an error here causes a blank menu entry in Nuke
    //if (!OFX::fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
    //    throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
    //}
}

void MixViewsPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add the supported contexts, only filter at the moment
    desc.addSupportedContext(eContextFilter);

    // add supported pixel depths
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
    // returning an error here crashes Nuke
    //if (!OFX::fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
    //  throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
    //}
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

void MixViewsPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/)
{
    if (!OFX::fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
        throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
    }

    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentXY);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentXY);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);
    
    // make some pages and to things in 
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // mix
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamMix);
        param->setLabel(kParamMixLabel);
        param->setHint(kParamMixHint);
        param->setDefault(0.);
        param->setIncrement(0.01);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        param->setDoubleType(eDoubleTypeScale);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
}

OFX::ImageEffect* MixViewsPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new MixViewsPlugin(handle);
}


static MixViewsPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
