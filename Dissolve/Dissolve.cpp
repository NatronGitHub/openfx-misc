/*
   OFX Dissolve plugin.

   Copyright (C) 2014 INRIA
   Author: Frederic Devernay <frederic.devernay@inria.fr>

   Redistribution and use in source and binary forms, with or without modification,
   are permitted provided that the following conditions are met:

   Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

   Redistributions in binary form must reproduce the above copyright notice, this
   list of conditions and the following disclaimer in the documentation and/or
   other materials provided with the distribution.

   Neither the name of the {organization} nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
   ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
   ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
   LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
   ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   INRIA
   Domaine de Voluceau
   Rocquencourt - B.P. 105
   78153 Le Chesnay Cedex - France

   Based on:
   OFX Cross Fade Transition example plugin, a plugin that illustrates the use of the OFX Support library.

   Copyright (C) 2004-2005 The Open Effects Association Ltd
   Author Bruno Nicoletti bruno@thefoundry.co.uk

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
 * Neither the name The Open Effects Association Ltd, nor the names of its
   contributors may be used to endorse or promote products derived from this
   software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
   ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
   ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
   LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
   ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   The Open Effects Association Ltd
   1 Wardour St
   London W1D 6PA
   England

 */

#include "Dissolve.h"

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"

#include "ofxsProcessing.H"
#include "ofxsImageBlenderMasked.h"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"

#define kPluginName "DissolveOFX"
#define kPluginGrouping "Merge"
#define kPluginDescription "Weighted average of two inputs"
#define kPluginIdentifier "net.sf.openfx.DissolvePlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe

#define kClipFrom "0"
#define kClipTo "1"

#define kParamWhich "which"
#define kParamWhichLabel "Which"
#define kParamWhichHint "Mix factor between the two inputs."

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class DissolvePlugin
    : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    DissolvePlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
          , dstClip_(0)
          , fromClip_(0)
          , toClip_(0)
          , maskClip_(0)
          , _transition(0)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        assert(dstClip_ && (dstClip_->getPixelComponents() == OFX::ePixelComponentRGB || dstClip_->getPixelComponents() == OFX::ePixelComponentRGBA || dstClip_->getPixelComponents() == OFX::ePixelComponentAlpha));
        fromClip_ = fetchClip(getContext() == OFX::eContextTransition ?  kOfxImageEffectTransitionSourceFromClipName : kClipFrom);
        assert(fromClip_ && (fromClip_->getPixelComponents() == OFX::ePixelComponentRGB || fromClip_->getPixelComponents() == OFX::ePixelComponentRGBA || fromClip_->getPixelComponents() == OFX::ePixelComponentAlpha));
        toClip_   = fetchClip(getContext() == OFX::eContextTransition ? kOfxImageEffectTransitionSourceToClipName : kClipTo);
        assert(toClip_ && (toClip_->getPixelComponents() == OFX::ePixelComponentRGB || toClip_->getPixelComponents() == OFX::ePixelComponentRGBA || toClip_->getPixelComponents() == OFX::ePixelComponentAlpha));
        maskClip_ = fetchClip("Mask");
        assert(!maskClip_ || maskClip_->getPixelComponents() == OFX::ePixelComponentAlpha);
        _transition = fetchDoubleParam(getContext() == OFX::eContextTransition ? kOfxImageEffectTransitionParamName : kParamWhich);
        assert(_transition);
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_maskInvert);
    }

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /* override is identity */
    virtual bool isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(OFX::ImageBlenderMaskedBase &, const OFX::RenderArguments &args);

private:

    template<int nComponents>
    void renderForComponents(const OFX::RenderArguments &args);

    template <class PIX, int nComponents, int maxValue>
    void renderForBitDepth(const OFX::RenderArguments &args);

    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *fromClip_;
    OFX::Clip *toClip_;
    OFX::Clip *maskClip_;
    OFX::DoubleParam* _transition;
    OFX::BooleanParam* _maskInvert;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

// make sure components are sane
static void
checkComponents(const OFX::Image &src,
                OFX::BitDepthEnum dstBitDepth,
                OFX::PixelComponentEnum dstComponents)
{
    OFX::BitDepthEnum srcBitDepth     = src.getPixelDepth();
    OFX::PixelComponentEnum srcComponents  = src.getPixelComponents();

    // see if they have the same depths and bytes and all
    if ( ( srcBitDepth != dstBitDepth) || ( srcComponents != dstComponents) ) {
        throw int(1); // HACK!! need to throw an sensible exception here!
    }
}

/* set up and run a processor */
void
DissolvePlugin::setupAndProcess(OFX::ImageBlenderMaskedBase &processor,
                                const OFX::RenderArguments &args)
{
    // get a dst image
    std::auto_ptr<OFX::Image>  dst( dstClip_->fetchImage(args.time) );
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dst->getRenderScale().x != args.renderScale.x ||
        dst->getRenderScale().y != args.renderScale.y ||
        dst->getField() != args.fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum dstBitDepth = dst->getPixelDepth();
    OFX::PixelComponentEnum dstComponents = dst->getPixelComponents();

    // fetch the two source images
    std::auto_ptr<OFX::Image> fromImg(fromClip_->fetchImage(args.time));
    std::auto_ptr<OFX::Image> toImg(toClip_->fetchImage(args.time));

    // make sure bit depths are sane
    if (fromImg.get()) {
        checkComponents(*fromImg, dstBitDepth, dstComponents);
        if (fromImg->getRenderScale().x != args.renderScale.x ||
            fromImg->getRenderScale().y != args.renderScale.y ||
            fromImg->getField() != args.fieldToRender) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }
    if (toImg.get()) {
        checkComponents(*toImg, dstBitDepth, dstComponents);
        if (toImg->getRenderScale().x != args.renderScale.x ||
            toImg->getRenderScale().y != args.renderScale.y ||
            toImg->getField() != args.fieldToRender) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }

    std::auto_ptr<OFX::Image> mask(maskClip_->fetchImage(args.time));
    if (mask.get()) {
        if (mask->getRenderScale().x != args.renderScale.x ||
            mask->getRenderScale().y != args.renderScale.y ||
            mask->getField() != args.fieldToRender) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }
    if (getContext() != OFX::eContextFilter &&
        getContext() != OFX::eContextTransition &&
        maskClip_->isConnected()) {
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    // get the transition value
    float blend = std::max(0., std::min(_transition->getValueAtTime(args.time), 1.));

    // set the images
    processor.setDstImg( dst.get() );
    processor.setFromImg( fromImg.get() );
    processor.setToImg( toImg.get() );

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    // set the scales
    processor.setBlend(blend);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

// the overridden render function
void
DissolvePlugin::render(const OFX::RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();

    // do the rendering
    if (dstComponents == OFX::ePixelComponentRGBA) {
        renderForComponents<4>(args);
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        renderForComponents<3>(args);
    }  else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        renderForComponents<1>(args);
    } // switch
} // render

template<int nComponents>
void
DissolvePlugin::renderForComponents(const OFX::RenderArguments &args)
{
    OFX::BitDepthEnum dstBitDepth    = dstClip_->getPixelDepth();
    switch (dstBitDepth) {
        case OFX::eBitDepthUByte:
            renderForBitDepth<unsigned char, nComponents, 255>(args);
            break;

        case OFX::eBitDepthUShort:
            renderForBitDepth<unsigned short, nComponents, 65535>(args);
            break;

        case OFX::eBitDepthFloat:
            renderForBitDepth<float, nComponents, 1>(args);
            break;
        default:
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

template <class PIX, int nComponents, int maxValue>
void
DissolvePlugin::renderForBitDepth(const OFX::RenderArguments &args)
{
    if (getContext() != OFX::eContextFilter &&
        getContext() != OFX::eContextTransition &&
        maskClip_->isConnected()) {
        OFX::ImageBlenderMasked<PIX, nComponents, maxValue, true> fred(*this);
        setupAndProcess(fred, args);
    } else {
        OFX::ImageBlenderMasked<PIX, nComponents, maxValue, false> fred(*this);
        setupAndProcess(fred, args);
    }
}

// overridden is identity
bool
DissolvePlugin::isIdentity(const OFX::IsIdentityArguments &args,
                           OFX::Clip * &identityClip,
                           double &identityTime)
{
    // get the transition value
    float blend = (float)_transition->getValueAtTime(args.time);

    identityTime = args.time;

    // at the start?
    if (blend <= 0.0) {
        identityClip = fromClip_;
        identityTime = args.time;

        return true;
    }

    // at the end?
    if (blend >= 1.0 &&
        (!maskClip_ || !maskClip_->isConnected())) {
        identityClip = toClip_;
        identityTime = args.time;

        return true;
    }

    // nope, identity we isnt
    return false;
}

bool
DissolvePlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    // get the transition value
    float blend = (float)_transition->getValueAtTime(args.time);
    // at the start?
    if (blend <= 0.0 && fromClip_ && fromClip_->isConnected()) {
        rod = fromClip_->getRegionOfDefinition(args.time);

        return true;
    }

    // at the end?
    if (blend >= 1.0 && toClip_ && toClip_->isConnected() &&
        (!maskClip_ || !maskClip_->isConnected())) {
        rod = toClip_->getRegionOfDefinition(args.time);

        return true;
    }

    if (fromClip_ && fromClip_->isConnected() && toClip_ && toClip_->isConnected()) {
        OfxRectD fromRoD = fromClip_->getRegionOfDefinition(args.time);
        OfxRectD toRoD = toClip_->getRegionOfDefinition(args.time);
        rod.x1 = std::min(fromRoD.x1, toRoD.x1);
        rod.y1 = std::min(fromRoD.y1, toRoD.y1);
        rod.x2 = std::max(fromRoD.x2, toRoD.x2);
        rod.y2 = std::max(fromRoD.y2, toRoD.y2);

        return true;
    }
    return false;
}

mDeclarePluginFactory(DissolvePluginFactory, {}, {}
                      );
using namespace OFX;

void
DissolvePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // Say we are a transition context
    desc.addSupportedContext(eContextTransition);
    desc.addSupportedContext(eContextGeneral);

    // Add supported pixel depths
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
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(kRenderThreadSafety);
}

void
DissolvePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                         ContextEnum context)
{
    // we are a transition, so define the sourceFrom input clip
    ClipDescriptor *fromClip = desc.defineClip(context == OFX::eContextTransition ? kOfxImageEffectTransitionSourceFromClipName : kClipFrom);

    fromClip->addSupportedComponent(ePixelComponentRGBA);
    fromClip->addSupportedComponent(ePixelComponentRGB);
    fromClip->addSupportedComponent(ePixelComponentAlpha);
    fromClip->setTemporalClipAccess(false);
    fromClip->setSupportsTiles(kSupportsTiles);

    // we are a transition, so define the sourceTo input clip
    ClipDescriptor *toClip = desc.defineClip(context == OFX::eContextTransition ? kOfxImageEffectTransitionSourceToClipName : kClipTo);
    toClip->addSupportedComponent(ePixelComponentRGBA);
    toClip->addSupportedComponent(ePixelComponentRGB);
    toClip->addSupportedComponent(ePixelComponentAlpha);
    toClip->setTemporalClipAccess(false);
    toClip->setSupportsTiles(kSupportsTiles);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    ClipDescriptor *maskClip = desc.defineClip("Mask");
    maskClip->addSupportedComponent(ePixelComponentAlpha);
    maskClip->setTemporalClipAccess(false);
    if (context == eContextGeneral) {
        maskClip->setOptional(true);
    }
    maskClip->setSupportsTiles(kSupportsTiles);
    maskClip->setIsMask(true);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // Define the mandated "Transition" param, note that we don't do anything with this other than.
    // describe it. It is not a true param but how the host indicates to the plug-in how far through
    // the transition it is. It appears on no plug-in side UI, it is purely the hosts to manage.
    if (context == OFX::eContextTransition) {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kOfxImageEffectTransitionParamName);
        // The host should have its own interface to the Transition param.
        // (range is 0-1)
        (void)param;
    } else {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamWhich);
        param->setLabels(kParamWhichLabel, kParamWhichLabel, kParamWhichLabel);
        param->setHint(kParamWhichHint);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        page->addChild(*param);
    }

    // don't define the mix param
    ofxsMaskDescribeParams(desc, page);
}

ImageEffect*
DissolvePluginFactory::createInstance(OfxImageEffectHandle handle,
                                      ContextEnum /*context*/)
{
    return new DissolvePlugin(handle);
}

void
getDissolvePluginID(OFX::PluginFactoryArray &ids)
{
    static DissolvePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);

    ids.push_back(&p);
}

