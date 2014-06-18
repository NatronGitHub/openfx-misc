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

namespace OFX {
  extern ImageEffectHostDescription gHostDescription;
}

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class TimeOffsetPlugin : public OFX::ImageEffect {
    protected :
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;            /**< @brief Mandated output clips */
    OFX::Clip *srcClip_;            /**< @brief Mandated input clips */

    OFX::IntParam  *time_offset_;      /**< @brief only used in the filter context. */
    OFX::BooleanParam  *reverse_input_;

    public :
    /** @brief ctor */
    TimeOffsetPlugin(OfxImageEffectHandle handle);
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args);

    /** Override the get frames needed action */
    virtual void getFramesNeeded(const OFX::FramesNeededArguments &args, OFX::FramesNeededSetter &frames);

    /* override the time domain action, only for the general context */
    virtual bool getTimeDomain(OfxRangeD &range);

    /* override is identity */
    virtual bool isIdentity(const OFX::RenderArguments &args, OFX::Clip * &identityClip, double &identityTime);
private:
    double getSourceTime(double time) const;
};

TimeOffsetPlugin::TimeOffsetPlugin(OfxImageEffectHandle handle)
: ImageEffect(handle)
, srcClip_(0)
, time_offset_(0)
, reverse_input_(0)
{
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);

    time_offset_   = fetchIntParam("time_offset");
    reverse_input_ = fetchBooleanParam("reverse_input");
}


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
    OFX::BitDepthEnum      srcBitDepth     = src.getPixelDepth();
    OFX::PixelComponentEnum srcComponents  = src.getPixelComponents();
        
    // see if they have the same depths and bytes and all
    if(srcBitDepth != dstBitDepth || srcComponents != dstComponents)
        OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
}

// figure the frame we should be retiming from
double
TimeOffsetPlugin::getSourceTime(double t) const
{
    double sourceTime = t - time_offset_->getValueAtTime(t); // no animation
    OfxRangeD range = srcClip_->getFrameRange();
    bool reverse_input = reverse_input_->getValueAtTime(t);
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
    if(getContext() == OFX::eContextGeneral) {
        // how many frames on the input clip
        OfxRangeD srcRange = srcClip_->getFrameRange();
        
        range.min = srcRange.min + time_offset_->getValueAtTime(srcRange.min);
        range.max = srcRange.max + time_offset_->getValueAtTime(srcRange.max);
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
    frames.setFramesNeeded(*srcClip_, range);
}

// the overridden render function
void
TimeOffsetPlugin::render(const OFX::RenderArguments &args)
{
    // do nothing as this should never be called as isIdentity should always be trapped
}

// overridden is identity
bool
TimeOffsetPlugin::isIdentity(const OFX::RenderArguments &args, OFX::Clip * &identityClip, double &identityTime)
{
    identityClip = srcClip_;
    identityTime = getSourceTime(args.time);
    return true;
}


using namespace OFX;

mDeclarePluginFactory(TimeOffsetPluginFactory, ;, {});

void TimeOffsetPluginFactory::load()
{
  // we can't be used on hosts that don't perfrom temporal clip access
  if(!gHostDescription.temporalClipAccess) {
    throw OFX::Exception::HostInadequate("Need random temporal image access to work");
  }
}

/** @brief The basic describe function, passed a plugin descriptor */
void TimeOffsetPluginFactory::describe(OFX::ImageEffectDescriptor &desc) 
{
  // basic labels
  desc.setLabels("TimeOffsetOFX", "TimeOffsetOFX", "TimeOffsetOFX");
  desc.setPluginGrouping("Time");
  desc.setPluginDescription("Move the input clip forward or backward in time. "
                            "This can also reverse the order of the input frames so that last one is first.");

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
  desc.setSupportsMultiResolution(true);
  desc.setSupportsTiles(true);
  desc.setTemporalClipAccess(true); // say we will be doing random time access on clips
  desc.setRenderTwiceAlways(false);
  desc.setSupportsMultipleClipPARs(false);

  // we can't be used on hosts that don't perfrom temporal clip access
  if(!gHostDescription.temporalClipAccess) {
    throw OFX::Exception::HostInadequate("Need random temporal image access to work");
  }
}        

/** @brief The describe in context function, passed a plugin descriptor and a context */
void TimeOffsetPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context) 
{
  // we are a transition, so define the sourceTo input clip
  ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
  srcClip->addSupportedComponent(ePixelComponentRGBA);
  srcClip->addSupportedComponent(ePixelComponentAlpha);
  srcClip->setTemporalClipAccess(true); // say we will be doing random time access on this clip
  srcClip->setSupportsTiles(true);

  // create the mandated output clip
  ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
  dstClip->addSupportedComponent(ePixelComponentRGBA);
  dstClip->addSupportedComponent(ePixelComponentAlpha);
  dstClip->setSupportsTiles(true);

  // make some pages and to things in 
  PageParamDescriptor *page = desc.definePageParam("Controls");

  IntParamDescriptor *time_offset = desc.defineIntParam("time_offset");
  time_offset->setLabels("time offset (frames)", "time offset (frames)", "time offset (frames)");
  time_offset->setScriptName("time_offset");
  time_offset->setHint("Offset in frames (frame f from the input will be at f+offset)");
  time_offset->setDefault(0);
  // keep default range (INT_MIN..INT_MAX)
  // no display range
  // time_offset->setDisplayRange(0, 0);
  time_offset->setAnimates(true);

  page->addChild(*time_offset);

  BooleanParamDescriptor *reverse_input = desc.defineBooleanParam("reverse_input");
  reverse_input->setDefault(false);
  reverse_input->setHint("Reverse the order of the input frames so that last one is first");
  reverse_input->setLabels("reverse input", "reverse input", "reverse input");
  reverse_input->setAnimates(true);

  page->addChild(*reverse_input);

}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* TimeOffsetPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum context)
{
  return new TimeOffsetPlugin(handle);
}

void getTimeOffsetPluginID(OFX::PluginFactoryArray &ids)
{
    static TimeOffsetPluginFactory p("net.sf.openfx:timeOffset", /*pluginVersionMajor=*/1, /*pluginVersionMinor=*/0);
    ids.push_back(&p);
}
