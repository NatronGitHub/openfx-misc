/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2017 INRIA
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
 * OFX TimeOffset plugin.
 * Move the input clip forward or backward in time.
 * This can also reverse the order of the input frames so that last one is first.
 */

#include "ofxsProcessing.H"
#include "ofxsMacros.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "TimeOffsetOFX"
#define kPluginGrouping "Time"
#define kPluginDescription "Move the input clip forward or backward in time. " \
    "This can also reverse the order of the input frames so that last one is first.\n" \
    "See also http://opticalenquiry.com/nuke/index.php?title=TimeOffset"

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
#define kParamTimeOffsetLabel "Time Offset (Frames)"
#define kParamTimeOffsetHint "Offset in frames (frame f from the input will be at f+offset)"

#define kParamReverseInput "reverseInput"
#define kParamReverseInputLabel "Reverse Input"
#define kParamReverseInputHint "Reverse the order of the input frames so that last one is first"

#define kParamClipToInputRange "clipToInputRange"
#define kParamClipToInputRangeLabel "Clip to Input Range"
#define kParamClipToInputRangeHint "Never ask for frames outside of the input frame range."

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class TimeOffsetPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    TimeOffsetPlugin(OfxImageEffectHandle handle);

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /** Override the get frames needed action */
    virtual void getFramesNeeded(const FramesNeededArguments &args, FramesNeededSetter &frames) OVERRIDE FINAL;

    /* override the time domain action, only for the general context */
    virtual bool getTimeDomain(OfxRangeD &range) OVERRIDE FINAL;

    /* override is identity */
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    double getSourceTime(double time) const;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    //Clip *_dstClip;            /**< @brief Mandated output clips */
    Clip *_srcClip;            /**< @brief Mandated input clips */
    IntParam  *_time_offset;      /**< @brief only used in the filter context. */
    BooleanParam  *_reverse_input;
    BooleanParam  *_clip;
};

TimeOffsetPlugin::TimeOffsetPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _srcClip(0)
    , _time_offset(0)
    , _reverse_input(0)
{
    _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
    assert( (!_srcClip && getContext() == eContextGenerator) ||
            ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentAlpha ||
                           _srcClip->getPixelComponents() == ePixelComponentRGB ||
                           _srcClip->getPixelComponents() == ePixelComponentRGBA) ) );

    _time_offset   = fetchIntParam(kParamTimeOffset);
    _reverse_input = fetchBooleanParam(kParamReverseInput);
    _clip = fetchBooleanParam(kParamClipToInputRange);
    assert(_time_offset && _reverse_input && _clip);
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
    if ( _clip->getValueAtTime(t) ) {
        // clip to min/max range
        if (sourceTime < range.min) {
            sourceTime = range.min;
        } else if (sourceTime > range.max) {
            sourceTime = range.max;
        }
    }

    return sourceTime;
}

/* override the time domain action, only for the general context */
bool
TimeOffsetPlugin::getTimeDomain(OfxRangeD &range)
{
    // this should only be called in the general context, ever!
    if (getContext() == eContextGeneral) {
        // how many frames on the input clip
        OfxRangeD srcRange = _srcClip->getFrameRange();

        range.min = srcRange.min + _time_offset->getValueAtTime(srcRange.min);
        range.max = srcRange.max + _time_offset->getValueAtTime(srcRange.max);

        return true;
    }

    return false;
}

void
TimeOffsetPlugin::getFramesNeeded(const FramesNeededArguments &args,
                                  FramesNeededSetter &frames)
{
    double sourceTime = getSourceTime(args.time);
    OfxRangeD range;

    range.min = sourceTime;
    range.max = sourceTime;
    frames.setFramesNeeded(*_srcClip, range);
}

// the overridden render function
void
TimeOffsetPlugin::render(const RenderArguments & /*args*/)
{
    // do nothing as this should never be called as isIdentity should always be trapped
}

// overridden is identity
bool
TimeOffsetPlugin::isIdentity(const IsIdentityArguments &args,
                             Clip * &identityClip,
                             double &identityTime)
{
    identityClip = _srcClip;
    identityTime = getSourceTime(args.time);

    return true;
}

bool
TimeOffsetPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                        OfxRectD &rod)
{
    const double identityTime = getSourceTime(args.time);

    rod = _srcClip->getRegionOfDefinition(identityTime);

    return true;
}

mDeclarePluginFactory(TimeOffsetPluginFactory,; , {});
void
TimeOffsetPluginFactory::load()
{
    // we can't be used on hosts that don't perfrom temporal clip access
    if (!getImageEffectHostDescription()->temporalClipAccess) {
        throw Exception::HostInadequate("Need random temporal image access to work");
    }
}

/** @brief The basic describe function, passed a plugin descriptor */
void
TimeOffsetPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // Say we are a filer context
    desc.addSupportedContext(eContextFilter);
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
    desc.setTemporalClipAccess(true); // say we will be doing random time access on clips
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
    // we can't be used on hosts that don't perfrom temporal clip access
    if (!getImageEffectHostDescription()->temporalClipAccess) {
        throw Exception::HostInadequate("Need random temporal image access to work");
    }
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void
TimeOffsetPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                           ContextEnum /*context*/)
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
        param->setRange(INT_MIN, INT_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(INT_MIN, INT_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setAnimates(false); // used in getTimeDomain()
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
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }

    // clip
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamClipToInputRange);
        param->setDefault(false);
        param->setHint(kParamClipToInputRangeHint);
        param->setLabel(kParamClipToInputRangeLabel);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
} // TimeOffsetPluginFactory::describeInContext

/** @brief The create instance function, the plugin must return an object derived from the \ref ImageEffect class */
ImageEffect*
TimeOffsetPluginFactory::createInstance(OfxImageEffectHandle handle,
                                        ContextEnum /*context*/)
{
    return new TimeOffsetPlugin(handle);
}

static TimeOffsetPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
