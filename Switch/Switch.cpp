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
 * OFX Switch plugin.
 * Switch between inputs.
 */

#include "Switch.h"

#include <string>
#include <algorithm>

#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsMacros.h"
#include "ofxNatron.h"
#include "ofxsCopier.h"

#ifdef OFX_EXTENSIONS_NUKE
#include "nuke/fnOfxExtensions.h"
#endif

#define kPluginName "SwitchOFX"
#define kPluginGrouping "Merge"
#define kPluginDescription "Lets you switch between any number of inputs.\n"\
"This plugin concatenates transforms."
#define kPluginIdentifier "net.sf.openfx.switchPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs true
#define kSupportsMultipleClipDepths true
#define kRenderThreadSafety eRenderFullySafe

#define kParamWhich "which"
#define kParamWhichLabel "Which"
#define kParamWhichHint \
"The input to display. Each input is displayed at the value corresponding to the number of the input. For example, setting which to 4 displays the image from input 4."

#define kClipSourceCount 16
#define kClipSourceCountNumerous 64

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class SwitchPlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    SwitchPlugin(OfxImageEffectHandle handle, bool numerousInputs);

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /* override is identity */
    virtual bool isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;

    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

#ifdef OFX_EXTENSIONS_NUKE
    /** @brief recover a transform matrix from an effect */
    virtual bool getTransform(const OFX::TransformArguments &args, OFX::Clip * &transformClip, double transformMatrix[9]) OVERRIDE FINAL;
#endif

    /** @brief get the clip preferences */
    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

private:

    void updateRange()
    {
        int maxconnected = 1;
        for (unsigned i = 2; i < _srcClip.size(); ++i) {
            if (_srcClip[i]->isConnected()) {
                maxconnected = i;
            }
        }
        _which->setDisplayRange(0, maxconnected);
    }

    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip* _dstClip;
    std::vector<OFX::Clip *> _srcClip;

    OFX::IntParam *_which;
};

SwitchPlugin::SwitchPlugin(OfxImageEffectHandle handle, bool numerousInputs)
: ImageEffect(handle)
, _dstClip(0)
, _srcClip(numerousInputs ? kClipSourceCountNumerous : kClipSourceCount)
, _which(0)
{
    _dstClip = fetchClip(kOfxImageEffectOutputClipName);
    assert(_dstClip && (_dstClip->getPixelComponents() == OFX::ePixelComponentAlpha || _dstClip->getPixelComponents() == OFX::ePixelComponentRGB || _dstClip->getPixelComponents() == OFX::ePixelComponentRGBA));
    for (unsigned i = 0; i < _srcClip.size(); ++i) {
        if (getContext() == OFX::eContextFilter && i == 0) {
            _srcClip[i] = fetchClip(kOfxImageEffectSimpleSourceClipName);
        } else {
            char name[3] = { 0, 0, 0 }; // don't use std::stringstream (not thread-safe on OSX)
            name[0] = (i < 10) ? ('0' + i) : ('0' + i / 10);
            name[1] = (i < 10) ?         0 : ('0' + i % 10);
            _srcClip[i] = fetchClip(name);
        }
        assert(_srcClip[i]);
    }
    _which  = fetchIntParam(kParamWhich);
    assert(_which);

    updateRange();
}

void
SwitchPlugin::render(const OFX::RenderArguments &args)
{
    // do nothing as this should never be called as isIdentity should always be trapped
    assert(false);

    // copy input to output
    int input;
    _which->getValueAtTime(args.time, input);
    input = std::max(0, std::min(input, (int)_srcClip.size()-1));
    OFX::Clip *srcClip = _srcClip[input];
    assert(kSupportsMultipleClipPARs   || !srcClip || srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || !srcClip || srcClip->getPixelDepth()       == _dstClip->getPixelDepth());
    // do the rendering
    std::auto_ptr<OFX::Image> dst(_dstClip->fetchImage(args.time));
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dst->getRenderScale().x != args.renderScale.x ||
        dst->getRenderScale().y != args.renderScale.y ||
        (dst->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && dst->getField() != args.fieldToRender)) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
    std::auto_ptr<const OFX::Image> src((srcClip && srcClip->isConnected()) ?
                                        srcClip->fetchImage(args.time) : 0);
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
}

// overridden is identity
bool
SwitchPlugin::isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &/*identityTime*/)
{
    int input;
    _which->getValueAtTime(args.time, input);
    input = std::max(0, std::min(input, (int)_srcClip.size()-1));
    identityClip = _srcClip[input];
    return true;
}

// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
void
SwitchPlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois)
{
    // this should never be called as isIdentity should always be trapped
    assert(false);
    int input;
    _which->getValueAtTime(args.time, input);
    input = std::max(0, std::min(input, (int)_srcClip.size()-1));
    const OfxRectD emptyRoI = {0., 0., 0., 0.};
    for (unsigned i = 0; i < _srcClip.size(); ++i) {
        if (i != (unsigned)input) {
            rois.setRegionOfInterest(*_srcClip[i], emptyRoI);
        }
    }
}

bool
SwitchPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    int input;
    _which->getValueAtTime(args.time, input);
    input = std::max(0, std::min(input, (int)_srcClip.size()-1));
    if (_srcClip[input] && _srcClip[input]->isConnected()) {
        rod = _srcClip[input]->getRegionOfDefinition(args.time);

        return true;
    }
    return false;
}


#ifdef OFX_EXTENSIONS_NUKE
// overridden getTransform
bool
SwitchPlugin::getTransform(const OFX::TransformArguments &args, OFX::Clip * &transformClip, double transformMatrix[9])
{
    int input;
    _which->getValueAtTime(args.time, input);
    input = std::max(0, std::min(input, (int)_srcClip.size()-1));
    transformClip = _srcClip[input];

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

/* Override the clip preferences */
void
SwitchPlugin::getClipPreferences(OFX::ClipPreferencesSetter &/*clipPreferences*/)
{
    updateRange();
}

void
SwitchPlugin::changedClip(const OFX::InstanceChangedArgs &/*args*/, const std::string &/*clipName*/)
{
    updateRange();
}


using namespace OFX;

mDeclarePluginFactory(SwitchPluginFactory, {}, {});

void SwitchPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add the supported contexts
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextFilter);

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
#ifdef OFX_EXTENSIONS_NUKE
    // Enable transform by the host.
    // It is only possible for transforms which can be represented as a 3x3 matrix.
    desc.setCanTransform(true);
#endif
    desc.setRenderThreadSafety(kRenderThreadSafety);
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

void SwitchPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    //Natron >= 2.0 allows multiple inputs to be folded like the viewer node, so use this to merge
    //more than 2 images
    bool numerousInputs =  (OFX::getImageEffectHostDescription()->isNatron &&
                            OFX::getImageEffectHostDescription()->versionMajor >= 2);

    int clipSourceCount = numerousInputs ? kClipSourceCountNumerous : kClipSourceCount;

    // Source clip only in the filter context
    // create the mandated source clip
    {
        ClipDescriptor *srcClip;
        if (context == eContextFilter) {
            srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
        } else {
            srcClip = desc.defineClip("0");
            srcClip->setOptional(true);
        }
        srcClip->addSupportedComponent(ePixelComponentNone);
        srcClip->addSupportedComponent(ePixelComponentRGB);
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentAlpha);
        srcClip->setTemporalClipAccess(false);
        srcClip->setSupportsTiles(kSupportsTiles);
        srcClip->setIsMask(false);
    }
    {
        ClipDescriptor *srcClip;
        srcClip = desc.defineClip("1");
        srcClip->setOptional(true);
        srcClip->addSupportedComponent(ePixelComponentNone);
        srcClip->addSupportedComponent(ePixelComponentRGB);
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentAlpha);
        srcClip->setTemporalClipAccess(false);
        srcClip->setSupportsTiles(kSupportsTiles);
        srcClip->setIsMask(false);
    }

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
            srcClip->setTemporalClipAccess(false);
            srcClip->setSupportsTiles(kSupportsTiles);
            srcClip->setIsMask(false);
        }
    }

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentNone);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // which
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamWhich);
        param->setLabel(kParamWhichLabel);
        param->setHint(kParamWhichHint);
        param->setDefault(0);
        param->setRange(0, clipSourceCount - 1);
        param->setDisplayRange(0, clipSourceCount - 1);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

#ifdef OFX_EXTENSIONS_NUKE
    // Enable transform by the host.
    // It is only possible for transforms which can be represented as a 3x3 matrix.
    desc.setCanTransform(true);
#endif
}

OFX::ImageEffect* SwitchPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    //Natron >= 2.0 allows multiple inputs to be folded like the viewer node, so use this to merge
    //more than 2 images
    bool numerousInputs =  (OFX::getImageEffectHostDescription()->isNatron &&
                            OFX::getImageEffectHostDescription()->versionMajor >= 2);

    return new SwitchPlugin(handle, numerousInputs);
}

void getSwitchPluginID(OFX::PluginFactoryArray &ids)
{
    static SwitchPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
