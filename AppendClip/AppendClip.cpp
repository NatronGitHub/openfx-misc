/*
   OFX AppendClip plugin.

   Copyright (C) 2015 INRIA
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

#include "AppendClip.h"

#include <cmath>

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"

#include "ofxsProcessing.H"
#include "ofxsImageBlender.H"
#include "ofxsCopier.h"
#include "ofxsMacros.h"
#include "ofxNatron.h"

#define kPluginName "AppendClipOFX"
#define kPluginGrouping "Time"
#define kPluginDescription "Append one clip to another."
#define kPluginIdentifier "net.sf.openfx.AppendClip"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamFadeIn "fadeIn"
#define kParamFadeInLabel "Fade In"
#define kParamFadeInHint "Number of frames to fade in from black at the beginning of the first clip."

#define kParamFadeOut "fadeOut"
#define kParamFadeOutLabel "Fade Out"
#define kParamFadeOutHint "Number of frames to fade out to black at the end of the last clip."

#define kParamCrossDissolve "crossDissolve"
#define kParamCrossDissolveLabel "Cross Dissolve"
#define kParamCrossDissolveHint "Number of frames to cross-dissolve between clips."

#define kParamCrossDissolve "crossDissolve"
#define kParamCrossDissolveLabel "Cross Dissolve"
#define kParamCrossDissolveHint "Number of frames to cross-dissolve between clips."

#define kParamFirstFrame "firstFrame"
#define kParamFirstFrameLabel "First Frame"
#define kParamFirstFrameHint "Frame to start the first clip at."

#define kParamLastFrame "lastFrame"
#define kParamLastFrameLabel "Last Frame"
#define kParamLastFrameHint "Last frame of the assembled clip (read-only)."

#define kClipSourceCount 10
#define kClipSourceOffset 1 // clip numbers start

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class AppendClipPlugin
    : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    AppendClipPlugin(OfxImageEffectHandle handle, bool numerousInputs)
        : ImageEffect(handle)
          , _dstClip(0)
          , _srcClip(numerousInputs ? kClipSourceCount : 2)
    , _fadeIn(0)
    , _fadeOut(0)
    , _crossDissolve(0)
    , _firstFrame(0)
    , _lastFrame(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == OFX::ePixelComponentRGB || _dstClip->getPixelComponents() == OFX::ePixelComponentRGBA || _dstClip->getPixelComponents() == OFX::ePixelComponentAlpha));
        for (unsigned i = 0; i < _srcClip.size(); ++i) {
            if (getContext() == OFX::eContextTransition && i < 2) {
                _srcClip[i] = fetchClip(i == 0 ? kOfxImageEffectTransitionSourceFromClipName : kOfxImageEffectTransitionSourceToClipName);
            } else {
                char name[3] = { 0, 0, 0 }; // don't use std::stringstream (not thread-safe on OSX)
                int clipNumber = i + kClipSourceOffset;
                name[0] = (clipNumber < 10) ? ('0' + clipNumber) : ('0' + clipNumber / 10);
                name[1] = (clipNumber < 10) ?                  0 : ('0' + clipNumber % 10);
                _srcClip[i] = fetchClip(name);
            }
            assert(_srcClip[i] && (_srcClip[i]->getPixelComponents() == OFX::ePixelComponentRGB || _srcClip[i]->getPixelComponents() == OFX::ePixelComponentRGBA || _srcClip[i]->getPixelComponents() == OFX::ePixelComponentAlpha));
        }

        _fadeIn = fetchIntParam(kParamFadeIn);
        _fadeOut = fetchIntParam(kParamFadeOut);
        _crossDissolve = fetchIntParam(kParamCrossDissolve);
        _firstFrame = fetchIntParam(kParamFirstFrame);
        _lastFrame = fetchIntParam(kParamLastFrame);
        assert(_fadeIn && _fadeOut && _crossDissolve && _firstFrame && _lastFrame);
    }

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /* override is identity */
    virtual bool isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;

    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    /* override the time domain action, only for the general context */
    virtual bool getTimeDomain(OfxRangeD &range) OVERRIDE FINAL;

private:
    /* set up and run a processor */
    void setupAndProcess(OFX::ImageBlenderBase &, const OFX::RenderArguments &args);

    void getSources(double time, int *clip0, double *t0, int *clip1, double *t1, double blend);

private:

    template<int nComponents>
    void renderForComponents(const OFX::RenderArguments &args);

    template <class PIX, int nComponents, int maxValue>
    void renderForBitDepth(const OFX::RenderArguments &args);

    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    std::vector<OFX::Clip *> _srcClip;
    OFX::IntParam* _fadeIn;
    OFX::IntParam* _fadeOut;
    OFX::IntParam* _crossDissolve;
    OFX::IntParam* _firstFrame;
    OFX::IntParam* _lastFrame;
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

void
AppendClipPlugin::getSources(double time,
                             int *clip0,
                             double *t0,
                             int *clip1,
                             double *t1,
                             double blend)
{
    // if fadeIn = 0 return first frame of first connected clip if time <= firstFrame, else return back
}

/* set up and run a processor */
void
AppendClipPlugin::setupAndProcess(OFX::ImageBlenderBase &processor,
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

#if 0
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
#endif
}

// the overridden render function
void
AppendClipPlugin::render(const OFX::RenderArguments &args)
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
AppendClipPlugin::renderForComponents(const OFX::RenderArguments &args)
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
AppendClipPlugin::renderForBitDepth(const OFX::RenderArguments &args)
{
    OFX::ImageBlender<PIX, nComponents> fred(*this);
    setupAndProcess(fred, args);
}

// overridden is identity
bool
AppendClipPlugin::isIdentity(const OFX::IsIdentityArguments &args,
                           OFX::Clip * &identityClip,
                           double &identityTime)
{
#if 0
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

    if ((which >= _srcClip.size() || (prev == which))) {
        identityClip = _srcClip[prev];
        identityTime = args.time;

        return true;
    }
#endif
    // nope, identity we isnt
    return false;
}

// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
void
AppendClipPlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois)
{
#if 0
    double which = std::max(0., std::min(_which->getValueAtTime(args.time), (double)_srcClip.size()-1));
    unsigned prev = std::floor(which);
    unsigned next = std::ceil(which);
    const OfxRectD emptyRoI = {0., 0., 0., 0.};
    for (unsigned i = 0; i < _srcClip.size(); ++i) {
        if (i != prev && i != next) {
            rois.setRegionOfInterest(*_srcClip[i], emptyRoI);
        }
    }
#endif
}

bool
AppendClipPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod)
{
#if 0
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
        _srcClip[prev] && _srcClip[prev]->isConnected()) {
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
#endif
    return false;
}

void
AppendClipPlugin::changedClip(const OFX::InstanceChangedArgs &/*args*/, const std::string &/*clipName*/)
{
    // TODO: set lastFrame
    int t;
    _firstFrame->getValue(t);
    for (unsigned i = 0; i < _srcClip.size(); ++i) {
        if (_srcClip[i]->isConnected()) {
            OfxRangeD r = _srcClip[i]->getFrameRange();
            if (r.max >= r.min) {
                t += 1 + (int)r.max - (int)r.min;
            }
        }
    }
    _lastFrame->setValue(t - 1);
}

/* override the time domain action, only for the general context */
bool
AppendClipPlugin::getTimeDomain(OfxRangeD &range)
{
    // this should only be called in the general context, ever!
    assert (getContext() == OFX::eContextGeneral);
    int t;
    _firstFrame->getValue(t);
    range.min = t;
    for (unsigned i = 0; i < _srcClip.size(); ++i) {
        if (_srcClip[i]->isConnected()) {
            OfxRangeD r = _srcClip[i]->getFrameRange();
            if (r.max >= r.min) {
                t += 1 + (int)r.max - (int)r.min;
            }
        }
    }
    range.max = t - 1;

    return true;
}

mDeclarePluginFactory(AppendClipPluginFactory, {}, {}
                      );
using namespace OFX;

void
AppendClipPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

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
AppendClipPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
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
            char name[3] = { 0, 0, 0 }; // don't use std::stringstream (not thread-safe on OSX)
            int i = 0;
            int clipNumber = i + kClipSourceOffset;
            name[0] = (clipNumber < 10) ? ('0' + clipNumber) : ('0' + clipNumber / 10);
            name[1] = (clipNumber < 10) ?                  0 : ('0' + clipNumber % 10);
            srcClip = desc.defineClip(name);
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
            char name[3] = { 0, 0, 0 }; // don't use std::stringstream (not thread-safe on OSX)
            int i = 1;
            int clipNumber = i + kClipSourceOffset;
            name[0] = (clipNumber < 10) ? ('0' + clipNumber) : ('0' + clipNumber / 10);
            name[1] = (clipNumber < 10) ?                  0 : ('0' + clipNumber % 10);
            srcClip = desc.defineClip(name);
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

    if (numerousInputs) {
        for (int i = 2; i < clipSourceCount; ++i) {
            assert(i < 100);
            ClipDescriptor *srcClip;
            char name[3] = { 0, 0, 0 }; // don't use std::stringstream (not thread-safe on OSX)
            int clipNumber = i + kClipSourceOffset;
            name[0] = (clipNumber < 10) ? ('0' + clipNumber) : ('0' + clipNumber / 10);
            name[1] = (clipNumber < 10) ?                  0 : ('0' + clipNumber % 10);
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

    {
        IntParamDescriptor *param = desc.defineIntParam(kParamFadeIn);
        param->setLabel(kParamFadeInLabel);
        param->setHint(kParamFadeInHint);
        param->setDisplayRange(0, 50);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamFadeOut);
        param->setLabel(kParamFadeOutLabel);
        param->setHint(kParamFadeOutHint);
        param->setDisplayRange(0, 50);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamCrossDissolve);
        param->setLabel(kParamCrossDissolveLabel);
        param->setHint(kParamCrossDissolveHint);
        param->setDisplayRange(0, 50);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamFirstFrame);
        param->setLabel(kParamFirstFrameLabel);
        param->setHint(kParamFirstFrameHint);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamLastFrame);
        param->setLabel(kParamLastFrameLabel);
        param->setHint(kParamLastFrameHint);
        param->setEnabled(false);
        if (page) {
            page->addChild(*param);
        }
    }
}

ImageEffect*
AppendClipPluginFactory::createInstance(OfxImageEffectHandle handle,
                                      ContextEnum /*context*/)
{
    //Natron >= 2.0 allows multiple inputs to be folded like the viewer node, so use this to merge
    //more than 2 images
    bool numerousInputs =  (OFX::getImageEffectHostDescription()->hostName != kNatronOfxHostName ||
                            (OFX::getImageEffectHostDescription()->hostName == kNatronOfxHostName &&
                             OFX::getImageEffectHostDescription()->versionMajor >= 2));

    return new AppendClipPlugin(handle, numerousInputs);
}

void
getAppendClipPluginID(OFX::PluginFactoryArray &ids)
{
    {
        //static AppendClipPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);

        //ids.push_back(&p);
    }
}

