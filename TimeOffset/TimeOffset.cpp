/*
 OFX TimeOffset plugin.
 Move the input clip forward or backward in time.
 This can also reverse the order of the input frames so that last one is first.

 Copyright (C) 2013 INRIA
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

#include "TimeOffset.h"

#include "ofxsProcessing.H"
#include "ofxsMacros.h"

#define kPluginName "TimeOffsetOFX"
#define kPluginGrouping "Time"
#define kPluginDescription "Move the input clip forward or backward in time. " \
                           "This can also reverse the order of the input frames so that last one is first."
#define kPluginIdentifier "net.sf.openfx.timeOffset"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamTimeOffset "timeOffset"
#define kParamTimeOffsetLabel "Time offset (frames)"
#define kParamTimeOffsetHint "Offset in frames (frame f from the input will be at f+offset)"
#define kParamReverseInput "reverseInput"
#define kParamReverseInputLabel "Reverse input"
#define kParamReverseInputHint "Reverse the order of the input frames so that last one is first"

namespace OFX {
    extern ImageEffectHostDescription gHostDescription;
}

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class TimeOffsetPlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    TimeOffsetPlugin(OfxImageEffectHandle handle);

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /** Override the get frames needed action */
    virtual void getFramesNeeded(const OFX::FramesNeededArguments &args, OFX::FramesNeededSetter &frames) OVERRIDE FINAL;

    /* override the time domain action, only for the general context */
    virtual bool getTimeDomain(OfxRangeD &range) OVERRIDE FINAL;

    /* override is identity */
    virtual bool isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    double getSourceTime(double time) const;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    //OFX::Clip *_dstClip;            /**< @brief Mandated output clips */
    OFX::Clip *_srcClip;            /**< @brief Mandated input clips */

    OFX::IntParam  *_time_offset;      /**< @brief only used in the filter context. */
    OFX::BooleanParam  *_reverse_input;
};

TimeOffsetPlugin::TimeOffsetPlugin(OfxImageEffectHandle handle)
: ImageEffect(handle)
, _srcClip(0)
, _time_offset(0)
, _reverse_input(0)
{
    _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
    assert((!_srcClip && getContext() == OFX::eContextGenerator) ||
           (_srcClip && (_srcClip->getPixelComponents() == OFX::ePixelComponentAlpha ||
                         _srcClip->getPixelComponents() == OFX::ePixelComponentRGB ||
                         _srcClip->getPixelComponents() == OFX::ePixelComponentRGBA)));

    _time_offset   = fetchIntParam(kParamTimeOffset);
    _reverse_input = fetchBooleanParam(kParamReverseInput);
    assert(_time_offset && _reverse_input);
}


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

// figure the frame we should be retiming from
double
TimeOffsetPlugin::getSourceTime(double t) const
{
    double sourceTime = t - _time_offset->getValueAtTime(t); // no animation
    if (!_srcClip) {
        return sourceTime;
    }
    OfxRangeD range = _srcClip->getFrameRange();
    bool reverse_input = _reverse_input->getValueAtTime(t);
    if (reverse_input) {
        sourceTime = range.max - sourceTime + range.min;
    }
    // clip to min/max range
    if (sourceTime < range.min) {
        sourceTime = range.min;
    } else if (sourceTime > range.max) {
        sourceTime = range.max;
    }
    return sourceTime;
}

/* override the time domain action, only for the general context */
bool
TimeOffsetPlugin::getTimeDomain(OfxRangeD &range)
{
    // this should only be called in the general context, ever!
    if (getContext() == OFX::eContextGeneral) {
        // how many frames on the input clip
        OfxRangeD srcRange = _srcClip->getFrameRange();

        range.min = srcRange.min + _time_offset->getValueAtTime(srcRange.min);
        range.max = srcRange.max + _time_offset->getValueAtTime(srcRange.max);
        return true;
    }

    return false;
}

void
TimeOffsetPlugin::getFramesNeeded(const OFX::FramesNeededArguments &args,
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
TimeOffsetPlugin::render(const OFX::RenderArguments &/*args*/)
{
    // do nothing as this should never be called as isIdentity should always be trapped
}

// overridden is identity
bool
TimeOffsetPlugin::isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &identityTime)
{
    identityClip = _srcClip;
    identityTime = getSourceTime(args.time);
    return true;
}


using namespace OFX;

mDeclarePluginFactory(TimeOffsetPluginFactory, ;, {});

void TimeOffsetPluginFactory::load()
{
    // we can't be used on hosts that don't perfrom temporal clip access
    if (!gHostDescription.temporalClipAccess) {
        throw OFX::Exception::HostInadequate("Need random temporal image access to work");
    }
}

/** @brief The basic describe function, passed a plugin descriptor */
void TimeOffsetPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
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
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void TimeOffsetPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum /*context*/)
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

    // time_offset
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamTimeOffset);
        param->setLabel(kParamTimeOffsetLabel);
        param->setHint(kParamTimeOffsetHint);
        param->setDefault(0);
        // keep default range (INT_MIN..INT_MAX)
        // no display range
        // param->setDisplayRange(0, 0);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // reverse_input
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamReverseInput);
        param->setDefault(false);
        param->setHint(kParamReverseInputHint);
        param->setLabel(kParamReverseInputLabel);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* TimeOffsetPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    return new TimeOffsetPlugin(handle);
}

void getTimeOffsetPluginID(OFX::PluginFactoryArray &ids)
{
    static TimeOffsetPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
