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
 * OFX FrameRange plugin.
 */

#include "FrameRange.h"

#include <algorithm> // for std::max
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxsCopier.h"

#ifdef OFX_EXTENSIONS_NUKE
#include "nuke/fnOfxExtensions.h"
#endif

#define kPluginName "FrameRangeOFX"
#define kPluginGrouping "Time"
#define kPluginDescription "Set the frame range for a clip. Useful in conjunction with AppendClipOFX."
#define kPluginIdentifier "net.sf.openfx.FrameRange"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths true
#define kRenderThreadSafety eRenderFullySafe

#define kParamFrameRange "frameRange"
#define kParamFrameRangeLabel "Frame Range"
#define kParamFrameRangeHint "Output frame range."

#define kParamReset "reset"
#define kParamResetLabel "Reset"
#define kParamResetHint "Resets the frame range to its initial value."

using namespace OFX;

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class FrameRangePlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    FrameRangePlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    , _frameRange(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        _frameRange = fetchInt2DParam(kParamFrameRange);
        assert(_frameRange);
    }

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

#ifdef OFX_EXTENSIONS_NUKE
    /** @brief recover a transform matrix from an effect */
    virtual bool getTransform(const OFX::TransformArguments &args, OFX::Clip * &transformClip, double transformMatrix[9]) OVERRIDE FINAL;
#endif

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

      /* override the time domain action, only for the general context */
    virtual bool getTimeDomain(OfxRangeD &range) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;

    OFX::Int2DParam *_frameRange;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


// the overridden render function
void
FrameRangePlugin::render(const OFX::RenderArguments &/*args*/)
{
    assert(false);
}

bool
FrameRangePlugin::isIdentity(const IsIdentityArguments &/*args*/, Clip * &identityClip, double &/*identityTime*/)
{
    identityClip = _srcClip;
    return true;
}

#ifdef OFX_EXTENSIONS_NUKE
// overridden getTransform
bool
FrameRangePlugin::getTransform(const OFX::TransformArguments &/*args*/, OFX::Clip * &transformClip, double transformMatrix[9])
{
    transformClip = _srcClip;
    transformMatrix[0] = 1.;
    transformMatrix[1] = 0.;
    transformMatrix[2] = 0.;
    transformMatrix[3] = 0.;
    transformMatrix[4] = 1.;
    transformMatrix[5] = 0.;
    transformMatrix[6] = 0.;
    transformMatrix[7] = 0.;
    transformMatrix[8] = 1.;

    return true;
}
#endif


void
FrameRangePlugin::changedParam(const OFX::InstanceChangedArgs &/*args*/, const std::string &paramName)
{
    if (paramName == kParamReset && _srcClip && _srcClip->isConnected()) {
        OfxRangeD range = _srcClip->getFrameRange();
        _frameRange->setValue((int)range.min, (int)range.max);
    }
}

/* override the time domain action, only for the general context */
bool
FrameRangePlugin::getTimeDomain(OfxRangeD &range)
{
    // this should only be called in the general context, ever!
    assert (getContext() == OFX::eContextGeneral);
    int min, max;
    _frameRange->getValue(min, max);
    range.min = min;
    range.max = std::max(min, max);
    return true;
}

using namespace OFX;

mDeclarePluginFactory(FrameRangePluginFactory, {}, {});

void FrameRangePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add the supported contexts, only general, because the only useful action is getTimeDomain
    desc.addSupportedContext(eContextGeneral);

    // add supported pixel depths
    desc.addSupportedBitDepth(eBitDepthNone);
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthHalf);
    desc.addSupportedBitDepth(eBitDepthFloat);
    desc.addSupportedBitDepth(eBitDepthCustom);
#ifdef OFX_EXTENSIONS_VEGAS
    desc.addSupportedBitDepth(eBitDepthUByteBGRA);
    desc.addSupportedBitDepth(eBitDepthUShortBGRA);
    desc.addSupportedBitDepth(eBitDepthFloatBGRA);
#endif

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
#ifdef OFX_EXTENSIONS_NUKE
    // Enable transform by the host.
    // It is only possible for transforms which can be represented as a 3x3 matrix.
    desc.setCanTransform(true);
    // ask the host to render all planes
    desc.setPassThroughForNotProcessedPlanes(ePassThroughLevelRenderAllRequestedPlanes);
#endif
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

void FrameRangePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentNone);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
#ifdef OFX_EXTENSIONS_NATRON
    srcClip->addSupportedComponent(ePixelComponentXY);
#endif
#ifdef OFX_EXTENSIONS_NUKE
    srcClip->setCanTransform(true);
#endif
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);
    
    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentNone);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
#ifdef OFX_EXTENSIONS_NATRON
    dstClip->addSupportedComponent(ePixelComponentXY);
#endif
    dstClip->setSupportsTiles(kSupportsTiles);
    
    // make some pages and to things in 
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // frameRange
    {
        Int2DParamDescriptor *param = desc.defineInt2DParam(kParamFrameRange);
        param->setLabel(kParamFrameRangeLabel);
        param->setHint(kParamFrameRangeHint);
        param->setDefault(1,1);
        param->setAnimates(false);
        param->setDimensionLabels("first", "last");
        param->setLayoutHint(eLayoutHintNoNewLine);
        if (page) {
            page->addChild(*param);
        }
    }
    // reset
    {
        PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamReset);
        param->setLabel(kParamResetLabel);
        param->setHint(kParamResetHint);
        if (page) {
            page->addChild(*param);
        }
    }
}

OFX::ImageEffect* FrameRangePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new FrameRangePlugin(handle);
}

void getFrameRangePluginID(OFX::PluginFactoryArray &ids)
{
    static FrameRangePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
