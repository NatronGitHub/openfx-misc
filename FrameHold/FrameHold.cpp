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
 * OFX FrameHold plugin.
 */

#include <cmath>
#include <stdio.h> // for snprintf & _snprintf
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#  include <windows.h>
#  if defined(_MSC_VER) && _MSC_VER < 1900
#    define snprintf _snprintf
#  endif
#endif // defined(_WIN32) || defined(__WIN32__) || defined(WIN32)

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxNatron.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

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

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class FrameHoldPlugin
    : public OFX::ImageEffect
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
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == OFX::ePixelComponentAlpha ||
                             _dstClip->getPixelComponents() == OFX::ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == OFX::ePixelComponentRGBA) );
        _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == OFX::eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  OFX::ePixelComponentAlpha ||
                               _srcClip->getPixelComponents() == OFX::ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == OFX::ePixelComponentRGBA) ) );

        _firstFrame = fetchIntParam(kParamFirstFrame);
        _increment = fetchIntParam(kParamIncrement);
        _sublabel = fetchStringParam(kNatronOfxParamStringSublabelName);
        assert(_firstFrame && _increment && _sublabel);
    }

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /** Override the get frames needed action */
    virtual void getFramesNeeded(const OFX::FramesNeededArguments &args, OFX::FramesNeededSetter &frames) OVERRIDE FINAL;

    /* override is identity */
    virtual bool isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &identityTime) OVERRIDE FINAL;
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

private:
    double getSourceTime(double time) const;

    void updateSublabel(double time);

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

    return firstFrame + increment * std::floor( (t - firstFrame) / increment );
}

void
FrameHoldPlugin::updateSublabel(double time)
{
    char label[80];
    int firstFrame, increment;

    _firstFrame->getValueAtTime(time, firstFrame);
    _increment->getValueAtTime(time, increment);
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
FrameHoldPlugin::render(const OFX::RenderArguments & /*args*/)
{
    // do nothing as this should never be called as isIdentity should always be trapped
}

// overridden is identity
bool
FrameHoldPlugin::isIdentity(const OFX::IsIdentityArguments &args,
                            OFX::Clip * &identityClip,
                            double &identityTime)
{
    identityClip = _srcClip;
    identityTime = getSourceTime(args.time);

    return true;
}

bool
FrameHoldPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args,
                                       OfxRectD &rod)
{
    rod = _srcClip->getRegionOfDefinition( getSourceTime(args.time) );

    return true;
}

void
FrameHoldPlugin::changedParam(const OFX::InstanceChangedArgs &args,
                              const std::string &paramName)
{
    if ( ( (paramName == kParamFirstFrame) || (paramName == kParamIncrement) ) && (args.reason == OFX::eChangeUserEdit) ) {
        updateSublabel(args.time);
    }
}

mDeclarePluginFactory(FrameHoldPluginFactory,; , {});
void
FrameHoldPluginFactory::load()
{
    // we can't be used on hosts that don't perfrom temporal clip access
    if (!getImageEffectHostDescription()->temporalClipAccess) {
        throw OFX::Exception::HostInadequate("Need random temporal image access to work");
    }
}

/** @brief The basic describe function, passed a plugin descriptor */
void
FrameHoldPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
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
    if (!getImageEffectHostDescription()->temporalClipAccess) {
        throw OFX::Exception::HostInadequate("Need random temporal image access to work");
    }
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void
FrameHoldPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
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

    // firstFrame
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamFirstFrame);
        param->setLabel(kParamFirstFrameLabel);
        param->setHint(kParamFirstFrameHint);
        param->setDefault(0);
        param->setRange(INT_MIN, INT_MAX);
        param->setDisplayRange(INT_MIN, INT_MAX);
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
        param->setRange(0, INT_MAX);
        param->setDisplayRange(0, INT_MAX);
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
        param->setIsPersistent(true);
        param->setEvaluateOnChange(false);
        param->setDefault("frame 0");
        if (page) {
            page->addChild(*param);
        }
    }
} // FrameHoldPluginFactory::describeInContext

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect*
FrameHoldPluginFactory::createInstance(OfxImageEffectHandle handle,
                                       ContextEnum /*context*/)
{
    return new FrameHoldPlugin(handle);
}

static FrameHoldPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
