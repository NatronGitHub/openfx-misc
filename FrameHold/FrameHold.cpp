/*
 OFX FrameHold plugin.

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


 The skeleton for this source file is from:
 OFX Basic Example plugin, a plugin that illustrates the use of the OFX Support library.

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

#include "FrameHold.h"

#include <cmath>

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxNatron.h"

#define kPluginName "FrameHoldOFX"
#define kPluginGrouping "Time"
#define kPluginDescription "Hold a given frame for the input clip indefinitely, or use a subsample of the input frames and hold them for several frames."
#define kPluginIdentifier "net.sf.openfx.FrameHold"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamFirstFrame "firstFrame"
#define kParamFirstFrameLabel "First Frame"
#define kParamFirstFrameHint "Reference input frame (the frame to hold if increment is 0)."
#define kParamIncrement "increment"
#define kParamIncrementLabel "Increment"
#define kParamIncrementHint "If increment is 0, only the \"firstFrame\" will be held. If it is positive, every multiple of \"increment\" plus \"firstFrame\" will be held for \"increment\" frames afterwards (before if it is negative)."

namespace OFX {
    extern ImageEffectHostDescription gHostDescription;
}

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class FrameHoldPlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    FrameHoldPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    , _firstFrame(0)
    , _increment(0)
    , _sublabel(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == OFX::ePixelComponentAlpha || _dstClip->getPixelComponents() == OFX::ePixelComponentRGB || _dstClip->getPixelComponents() == OFX::ePixelComponentRGBA));
        _srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(_srcClip && (_srcClip->getPixelComponents() == OFX::ePixelComponentAlpha || _srcClip->getPixelComponents() == OFX::ePixelComponentRGB || _srcClip->getPixelComponents() == OFX::ePixelComponentRGBA));

        _firstFrame = fetchIntParam(kParamFirstFrame);
        _increment = fetchIntParam(kParamIncrement);
        _sublabel = fetchStringParam(kNatronOfxParamStringSublabelName);
        assert(_firstFrame && _increment);
    }

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /** Override the get frames needed action */
    virtual void getFramesNeeded(const OFX::FramesNeededArguments &args, OFX::FramesNeededSetter &frames) OVERRIDE FINAL;

    /* override is identity */
    virtual bool isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

private:
    double getSourceTime(double time) const;

    void updateSublabel();

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;            /**< @brief Mandated output clips */
    OFX::Clip *_srcClip;            /**< @brief Mandated input clips */

    OFX::IntParam  *_firstFrame;
    OFX::IntParam  *_increment;
    OFX::StringParam *_sublabel;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

// figure the frame we should be retiming from
double
FrameHoldPlugin::getSourceTime(double t) const
{
    int firstFrame, increment;
    _firstFrame->getValueAtTime(t, firstFrame);
    _increment->getValueAtTime(t, increment);

    if (increment == 0) {
        return firstFrame;
    }

    return firstFrame + increment * std::floor((t - firstFrame)/increment);
}

void
FrameHoldPlugin::updateSublabel()
{
    char label[80];

    int firstFrame, increment;
    _firstFrame->getValue(firstFrame);
    _increment->getValue(increment);
    if (increment == 0) {
        snprintf(label, sizeof(label), "frame %d", firstFrame);
    } else {
        snprintf(label, sizeof(label), "frame %d+n*%d", firstFrame, increment);
    }
    _sublabel->setValue(label);
}

void
FrameHoldPlugin::getFramesNeeded(const OFX::FramesNeededArguments &args,
                                  OFX::FramesNeededSetter &frames)
{
    double sourceTime = getSourceTime(args.time);
    OfxRangeD range;
    range.min = sourceTime;
    range.max = sourceTime;
    frames.setFramesNeeded(*_srcClip, range);
}

// the overridden render function
void
FrameHoldPlugin::render(const OFX::RenderArguments &/*args*/)
{
    // do nothing as this should never be called as isIdentity should always be trapped
}

// overridden is identity
bool
FrameHoldPlugin::isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &identityTime)
{
    identityClip = _srcClip;
    identityTime = getSourceTime(args.time);
    return true;
}

void
FrameHoldPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if ((paramName == kParamFirstFrame || paramName == kParamIncrement) && args.reason == OFX::eChangeUserEdit) {
        updateSublabel();
    }
}

using namespace OFX;

mDeclarePluginFactory(FrameHoldPluginFactory, ;, {});

void FrameHoldPluginFactory::load()
{
    // we can't be used on hosts that don't perfrom temporal clip access
    if (!gHostDescription.temporalClipAccess) {
        throw OFX::Exception::HostInadequate("Need random temporal image access to work");
    }
}

/** @brief The basic describe function, passed a plugin descriptor */
void FrameHoldPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // Say we are a filer context
    desc.addSupportedContext(OFX::eContextFilter);
    desc.addSupportedContext(OFX::eContextGeneral);

    // Add supported pixel depths
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(true); // say we will be doing random time access on clips
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
    // we can't be used on hosts that don't perfrom temporal clip access
    if (!gHostDescription.temporalClipAccess) {
        throw OFX::Exception::HostInadequate("Need random temporal image access to work");
    }
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void FrameHoldPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum /*context*/)
{
    // we are a transition, so define the sourceTo input clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(true); // say we will be doing random time access on this clip
    srcClip->setSupportsTiles(kSupportsTiles);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // firstFrame
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamFirstFrame);
        param->setLabel(kParamFirstFrameLabel);
        param->setHint(kParamFirstFrameHint);
        param->setDefault(0);
        // keep default range (INT_MIN..INT_MAX)
        // no display range
        // param->setDisplayRange(0, 0);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // increment
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamIncrement);
        param->setLabel(kParamIncrementLabel);
        param->setHint(kParamIncrementHint);
        param->setDefault(0);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // sublabel
    {
        StringParamDescriptor* param = desc.defineStringParam(kNatronOfxParamStringSublabelName);
        param->setIsSecret(true); // always secret
        param->setEnabled(false);
        param->setIsPersistant(true);
        param->setEvaluateOnChange(false);
        param->setDefault("frame 0");
        if (page) {
            page->addChild(*param);
        }
    }
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* FrameHoldPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    return new FrameHoldPlugin(handle);
}

void getFrameHoldPluginID(OFX::PluginFactoryArray &ids)
{
    static FrameHoldPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
