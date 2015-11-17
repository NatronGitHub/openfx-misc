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
 * OFX OneView plugin.
 * Takes one view from the input.
 */

#include "OneView.h"

#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxsCopier.h"

#define kPluginName "OneViewOFX"
#define kPluginGrouping "Views"
#define kPluginDescription "Takes one view from the input."
#define kPluginIdentifier "net.sf.openfx.oneViewPlugin"
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamView "view"
#define kParamViewLabel "View"
#define kParamViewHint "View to take from the input"
#define kParamViewOptionLeft "Left"
#define kParamViewOptionRight "Right"

using namespace OFX;

static bool gHostSupportsDynamicChoices = false;

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class OneViewPlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    OneViewPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    , _view(0)
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
        _view = fetchChoiceParam(kParamView);
        assert(_view);
    }

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;
    
    template <int nComponents>
    void renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth);

    /** @brief get the frame/views needed for input clips*/
    virtual void getFrameViewsNeeded(const FrameViewsNeededArguments& args, FrameViewsNeededSetter& frameViews) OVERRIDE FINAL;

    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;
    
    /* set up and run a processor */
    void setupAndProcess(PixelProcessorFilterBase &, const OFX::RenderArguments &args);
    
    //Cannot be implemented because the OpenFX API does not allow to return an identity view different from the calling view
    //virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;

    OFX::ChoiceParam     *_view;
    
    std::vector<std::string> _knownViews;
};

void
OneViewPlugin::getClipPreferences(OFX::ClipPreferencesSetter &/*clipPreferences*/)
{
    //Rebuild view choice
    int nViews = getViewCount();
    std::vector<std::string> views;
    for (int i = 0; i < nViews; ++i) {
        std::string view = getViewName(i);
        views.push_back(view);
    }
    
    if (views.size() == _knownViews.size()) {
        bool viewsChanged = false;
        for (std::size_t i = 0; i < _knownViews.size(); ++i) {
            if (_knownViews[i] != views[i]) {
                viewsChanged = true;
                break;
            }
        }
        if (!viewsChanged) {
            return;
        }
    }
    _knownViews = views;
    
    _view->resetOptions();
    for (std::size_t i = 0; i < views.size(); ++i) {
        _view->appendOption(views[i]);
    }
}


void
OneViewPlugin::getFrameViewsNeeded(const FrameViewsNeededArguments& args, FrameViewsNeededSetter& frameViews)
{
    int view;
    _view->getValueAtTime(args.time, view);
    
    OfxRangeD range;
    range.min = range.max = args.time;
    
    frameViews.addFrameViewsNeeded(*_srcClip,range , view);
}


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


/* set up and run a processor */
void
OneViewPlugin::setupAndProcess(PixelProcessorFilterBase &processor, const OFX::RenderArguments &args)
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

    int view;
    _view->getValueAtTime(args.time, view);
    // fetch main input image
    std::auto_ptr<const OFX::Image> src((_srcClip && _srcClip->isConnected()) ?
                                        _srcClip->fetchStereoscopicImage(args.time, view) : 0);

    // make sure bit depths are sane
    if (src.get()) {
        if (src->getRenderScale().x != args.renderScale.x ||
            src->getRenderScale().y != args.renderScale.y ||
            (src->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && src->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();

        // see if they have the same depths and bytes and all
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
    } else {
        setPersistentMessage(OFX::Message::eMessageError, "", "Failed to fetch source image");
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    // set the images
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

// the internal render function
template <int nComponents>
void
OneViewPlugin::renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
        case OFX::eBitDepthUByte: {
            PixelCopier<unsigned char, nComponents> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case OFX::eBitDepthUShort: {
            PixelCopier<unsigned short, nComponents> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case OFX::eBitDepthFloat: {
            PixelCopier<float, nComponents> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        default:
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
OneViewPlugin::render(const OFX::RenderArguments &args)
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


using namespace OFX;

mDeclarePluginFactory(OneViewPluginFactory, ;, {});

void OneViewPluginFactory::load()
{
    // we can't be used on hosts that don't support the stereoscopic suite
    // returning an error here causes a blank menu entry in Nuke
    //if (!OFX::fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
    //    throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
    //}
}

void OneViewPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
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

    //We only render color plane
    desc.setIsMultiPlanar(false);
    
    //We're using the view calls (i.e: getFrameViewsNeeded)
    desc.setIsViewAware(true);
    
    //We render the same thing on all views
    desc.setIsViewInvariant(OFX::eViewInvarianceAllViewsInvariant);
    
    // returning an error here crashes Nuke
    //if (!OFX::fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
    //  throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
    //}
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

void OneViewPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/)
{
    
    if (!OFX::fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true) &&
        !OFX::fetchSuite(kFnOfxImageEffectPlaneSuite, 2, true)) {
        throwHostMissingSuiteException(kFnOfxImageEffectPlaneSuite);
    }
#ifdef OFX_EXTENSIONS_NATRON
    gHostSupportsDynamicChoices = OFX::getImageEffectHostDescription()->supportsDynamicChoices;
#endif

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

    // view
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamView);
        param->setLabel(kParamViewLabel);
        param->setHint(kParamViewHint);
        param->appendOption(kParamViewOptionLeft);
        param->appendOption(kParamViewOptionRight);
        param->setDefault(0);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
}

OFX::ImageEffect* OneViewPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new OneViewPlugin(handle);
}

void getOneViewPluginID(OFX::PluginFactoryArray &ids)
{
    static OneViewPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
