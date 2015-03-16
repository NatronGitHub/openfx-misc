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

#include <cmath>

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"

#include "ofxsProcessing.H"
#include "ofxsImageBlenderMasked.h"
#include "ofxsMaskMix.h"
#include "ofxsCopier.h"
#include "ofxsMacros.h"
#include "ofxNatron.h"

#define kPluginName "DissolveOFX"
#define kPluginGrouping "Merge"
#define kPluginDescription "Weighted average of two inputs."
#define kPluginIdentifier "net.sf.openfx.DissolvePlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamWhich "which"
#define kParamWhichLabel "Which"
#define kParamWhichHint "Mix factor between the inputs."

#define kClipSourceCount 10

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class DissolvePlugin
    : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    DissolvePlugin(OfxImageEffectHandle handle, bool numerousInputs)
        : ImageEffect(handle)
          , _dstClip(0)
          , _srcClip(numerousInputs ? kClipSourceCount : 2)
          , _which(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == OFX::ePixelComponentRGB || _dstClip->getPixelComponents() == OFX::ePixelComponentRGBA || _dstClip->getPixelComponents() == OFX::ePixelComponentAlpha));
        for (unsigned i = 0; i < _srcClip.size(); ++i) {
            if (getContext() == OFX::eContextTransition && i < 2) {
                _srcClip[i] = fetchClip(i == 0 ? kOfxImageEffectTransitionSourceFromClipName : kOfxImageEffectTransitionSourceToClipName);
            } else {
                char name[3] = { 0, 0, 0 }; // don't use std::stringstream (not thread-safe on OSX)
                name[0] = (i < 10) ? ('0' + i) : ('0' + i / 10);
                name[1] = (i < 10) ?         0 : ('0' + i % 10);
                _srcClip[i] = fetchClip(name);
            }
            assert(_srcClip[i] && (_srcClip[i]->getPixelComponents() == OFX::ePixelComponentRGB || _srcClip[i]->getPixelComponents() == OFX::ePixelComponentRGBA || _srcClip[i]->getPixelComponents() == OFX::ePixelComponentAlpha));
        }

        _maskClip = fetchClip("Mask");
        assert(!_maskClip || _maskClip->getPixelComponents() == OFX::ePixelComponentAlpha);
        _which = fetchDoubleParam(getContext() == OFX::eContextTransition ? kOfxImageEffectTransitionParamName : kParamWhich);
        assert(_which);
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_maskInvert);

        int maxconnected = 1;
        for (unsigned i = 0; i < _srcClip.size(); ++i) {
            if (_srcClip[i]->isConnected()) {
                maxconnected = i;
            }
        }
        _which->setDisplayRange(0, maxconnected);
    }

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /* override is identity */
    virtual bool isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;

    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(OFX::ImageBlenderMaskedBase &, const OFX::RenderArguments &args);

private:

    template<int nComponents>
    void renderForComponents(const OFX::RenderArguments &args);

    template <class PIX, int nComponents, int maxValue>
    void renderForBitDepth(const OFX::RenderArguments &args);

    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    std::vector<OFX::Clip *> _srcClip;
    OFX::Clip *_maskClip;
    OFX::DoubleParam* _which;
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
        OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
    }
}

/* set up and run a processor */
void
DissolvePlugin::setupAndProcess(OFX::ImageBlenderMaskedBase &processor,
                                const OFX::RenderArguments &args)
{
    // get a dst image
    std::auto_ptr<OFX::Image>  dst( _dstClip->fetchImage(args.time) );
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

    // get the transition value
    double which = std::max(0., std::min(_which->getValueAtTime(args.time), (double)_srcClip.size()-1));
    int prev = std::floor(which);
    int next = std::ceil(which);

    if (prev == next) {
        std::auto_ptr<const OFX::Image> src((_srcClip[prev] && _srcClip[prev]->isConnected()) ?
                                            _srcClip[prev]->fetchImage(args.time) : 0);
        if (src.get()) {
            if (src->getRenderScale().x != args.renderScale.x ||
                src->getRenderScale().y != args.renderScale.y ||
                (src->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && src->getField() != args.fieldToRender)) {
                setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                OFX::throwSuiteStatusException(kOfxStatFailed);
            }
            OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
            OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
            if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
                OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
            }
        }
        copyPixels(*this, args.renderWindow, src.get(), dst.get());
        return;
    }

    // fetch the two source images
    std::auto_ptr<const OFX::Image> fromImg((_srcClip[prev] && _srcClip[prev]->isConnected()) ?
                                            _srcClip[prev]->fetchImage(args.time) : 0);
    std::auto_ptr<const OFX::Image> toImg((_srcClip[next] && _srcClip[next]->isConnected()) ?
                                          _srcClip[next]->fetchImage(args.time) : 0);

    // make sure bit depths are sane
    if (fromImg.get()) {
        if (fromImg->getRenderScale().x != args.renderScale.x ||
            fromImg->getRenderScale().y != args.renderScale.y ||
            (fromImg->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && fromImg->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        checkComponents(*fromImg, dstBitDepth, dstComponents);
    }
    if (toImg.get()) {
        if (toImg->getRenderScale().x != args.renderScale.x ||
            toImg->getRenderScale().y != args.renderScale.y ||
            (toImg->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && toImg->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        checkComponents(*toImg, dstBitDepth, dstComponents);
    }

    std::auto_ptr<const OFX::Image> mask((getContext() != OFX::eContextFilter && _maskClip && _maskClip->isConnected()) ?
                                         _maskClip->fetchImage(args.time) : 0);
    if (mask.get()) {
        if (mask->getRenderScale().x != args.renderScale.x ||
            mask->getRenderScale().y != args.renderScale.y ||
            (mask->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && mask->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }
    if (getContext() != OFX::eContextFilter &&
        getContext() != OFX::eContextTransition &&
        _maskClip && _maskClip->isConnected()) {
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }


    // set the images
    processor.setDstImg( dst.get() );
    processor.setFromImg( fromImg.get() );
    processor.setToImg( toImg.get() );

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    // set the scales
    processor.setBlend(which - prev);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

// the overridden render function
void
DissolvePlugin::render(const OFX::RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    for (unsigned i = 0; i < _srcClip.size(); ++i) {
        assert(kSupportsMultipleClipPARs   || _srcClip[i]->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
        assert(kSupportsMultipleClipDepths || _srcClip[i]->getPixelDepth()       == _dstClip->getPixelDepth());
    }
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
    OFX::BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
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
        _maskClip && _maskClip->isConnected()) {
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
    double which = std::max(0., std::min(_which->getValueAtTime(args.time), (double)_srcClip.size()-1));
    int prev = (int)which;
    //int next = std::min((int)which+1,(int)_srcClip.size()-1);

    identityTime = args.time;

    // at the start?
    if (which <= 0.0) {
        identityClip = _srcClip[0];
        identityTime = args.time;

        return true;
    }

    if ((which >= _srcClip.size() || (prev == which)) &&
        (!_maskClip || !_maskClip->isConnected())) {
        identityClip = _srcClip[prev];
        identityTime = args.time;

        return true;
    }

    // nope, identity we isnt
    return false;
}

// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
void
DissolvePlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois)
{
    double which = std::max(0., std::min(_which->getValueAtTime(args.time), (double)_srcClip.size()-1));
    unsigned prev = std::floor(which);
    unsigned next = std::ceil(which);
    const OfxRectD emptyRoI = {0., 0., 0., 0.};
    for (unsigned i = 0; i < _srcClip.size(); ++i) {
        if (i != prev && i != next) {
            rois.setRegionOfInterest(*_srcClip[i], emptyRoI);
        }
    }
}

bool
DissolvePlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    // get the transition value
    double which = std::max(0., std::min(_which->getValueAtTime(args.time), (double)_srcClip.size()-1));
    int prev = (int)which;
    int next = std::min((int)which+1,(int)_srcClip.size()-1);

    // at the start?
    if (which <= 0.0 && _srcClip[0] && _srcClip[0]->isConnected()) {
        rod = _srcClip[0]->getRegionOfDefinition(args.time);

        return true;
    }

    // at the end?
    if ((which >= _srcClip.size() || (which == prev)) &&
        _srcClip[prev] && _srcClip[prev]->isConnected() &&
        (!_maskClip || !_maskClip->isConnected())) {
        rod = _srcClip[prev]->getRegionOfDefinition(args.time);

        return true;
    }

    if (_srcClip[prev] && _srcClip[prev]->isConnected() && _srcClip[next] && _srcClip[next]->isConnected()) {
        OfxRectD fromRoD = _srcClip[prev]->getRegionOfDefinition(args.time);
        OfxRectD toRoD = _srcClip[next]->getRegionOfDefinition(args.time);
        rod.x1 = std::min(fromRoD.x1, toRoD.x1);
        rod.y1 = std::min(fromRoD.y1, toRoD.y1);
        rod.x2 = std::max(fromRoD.x2, toRoD.x2);
        rod.y2 = std::max(fromRoD.y2, toRoD.y2);

        return true;
    }
    return false;
}

void
DissolvePlugin::changedClip(const OFX::InstanceChangedArgs &/*args*/, const std::string &/*clipName*/)
{
    int maxconnected = 1;
    for (unsigned i = 0; i < _srcClip.size(); ++i) {
        if (_srcClip[i]->isConnected()) {
            maxconnected = i;
        }
    }
    _which->setDisplayRange(0, maxconnected);
}

mDeclarePluginFactory(DissolvePluginFactory, {}, {}
                      );
using namespace OFX;

void
DissolvePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
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
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
}

void
DissolvePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                         ContextEnum context)
{
    //Natron >= 2.0 allows multiple inputs to be folded like the viewer node, so use this to merge
    //more than 2 images
    bool numerousInputs =  (OFX::getImageEffectHostDescription()->hostName != kNatronOfxHostName ||
                            (OFX::getImageEffectHostDescription()->hostName == kNatronOfxHostName &&
                             OFX::getImageEffectHostDescription()->versionMajor >= 2));

    int clipSourceCount = numerousInputs ? kClipSourceCount : 2;

    {
        ClipDescriptor *srcClip;
        if (context == eContextTransition) {
            // we are a transition, so define the sourceFrom/sourceTo input clip
            srcClip = desc.defineClip(kOfxImageEffectTransitionSourceFromClipName);
        } else {
            srcClip = desc.defineClip("0");
            srcClip->setOptional(true);
        }
        srcClip->addSupportedComponent(ePixelComponentNone);
        srcClip->addSupportedComponent(ePixelComponentRGB);
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentAlpha);
        srcClip->addSupportedComponent(ePixelComponentCustom);
        srcClip->setTemporalClipAccess(false);
        srcClip->setSupportsTiles(kSupportsTiles);
        srcClip->setIsMask(false);
    }
    {
        ClipDescriptor *srcClip;
        if (context == eContextTransition) {
            // we are a transition, so define the sourceFrom/sourceTo input clip
            srcClip = desc.defineClip(kOfxImageEffectTransitionSourceToClipName);
        } else {
            srcClip = desc.defineClip("1");
            srcClip->setOptional(true);
        }
        srcClip->addSupportedComponent(ePixelComponentNone);
        srcClip->addSupportedComponent(ePixelComponentRGB);
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentAlpha);
        srcClip->addSupportedComponent(ePixelComponentCustom);
        srcClip->setTemporalClipAccess(false);
        srcClip->setSupportsTiles(kSupportsTiles);
        srcClip->setIsMask(false);
    }

    ClipDescriptor *maskClip = desc.defineClip("Mask");
    maskClip->addSupportedComponent(ePixelComponentAlpha);
    maskClip->setTemporalClipAccess(false);
    if (context == eContextGeneral) {
        maskClip->setOptional(true);
    }
    maskClip->setSupportsTiles(kSupportsTiles);
    maskClip->setIsMask(true);

    if (numerousInputs) {
        for (int i = 2; i < clipSourceCount; ++i) {
            assert(i < 100);
            ClipDescriptor *srcClip;
            char name[3] = { 0, 0, 0 }; // don't use std::stringstream (not thread-safe on OSX)
            name[0] = (i < 10) ? ('0' + i) : ('0' + i / 10);
            name[1] = (i < 10) ?         0 : ('0' + i % 10);
            srcClip = desc.defineClip(name);
            srcClip->setOptional(true);
            srcClip->addSupportedComponent(ePixelComponentNone);
            srcClip->addSupportedComponent(ePixelComponentRGB);
            srcClip->addSupportedComponent(ePixelComponentRGBA);
            srcClip->addSupportedComponent(ePixelComponentAlpha);
            srcClip->addSupportedComponent(ePixelComponentCustom);
            srcClip->setTemporalClipAccess(false);
            srcClip->setSupportsTiles(kSupportsTiles);
            srcClip->setIsMask(false);
        }
    }

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);


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
        param->setLabel(kParamWhichLabel);
        param->setHint(kParamWhichHint);
        param->setRange(0., clipSourceCount);
        param->setDisplayRange(0., 1.);
        if (page) {
            page->addChild(*param);
        }
    }

    // don't define the mix param
    ofxsMaskDescribeParams(desc, page);
}

ImageEffect*
DissolvePluginFactory::createInstance(OfxImageEffectHandle handle,
                                      ContextEnum /*context*/)
{
    //Natron >= 2.0 allows multiple inputs to be folded like the viewer node, so use this to merge
    //more than 2 images
    bool numerousInputs =  (OFX::getImageEffectHostDescription()->hostName != kNatronOfxHostName ||
                            (OFX::getImageEffectHostDescription()->hostName == kNatronOfxHostName &&
                             OFX::getImageEffectHostDescription()->versionMajor >= 2));

    return new DissolvePlugin(handle, numerousInputs);
}

void
getDissolvePluginID(OFX::PluginFactoryArray &ids)
{
    static DissolvePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);

    ids.push_back(&p);
}

