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
 * OFX TestOpenGL plugin.
 */

#if defined(OFX_SUPPORTS_OPENGLRENDER) || defined(HAVE_OSMESA) // at least one is required for this plugin

#include "TestOpenGL.h"

#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsImageEffect.h"
#include "ofxsMacros.h"
#include "ofxOpenGLRender.h"

#define kPluginName "TestOpenGL"
#define kPluginGrouping "Other/Test"
#define kPluginDescription \
"Test OpenGL rendering.\n" \
"This plugin draws a 200x100 red square at (10,10)."

#define kPluginIdentifier "net.sf.openfx.TestOpenGL"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 0
#define kSupportsMultiResolution 0
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamScale "scale"
#define kParamScaleLabel "Scale"
#define kParamScaleHint "Scales the red rect"

#define kParamSourceScale "sourceScale"
#define kParamSourceScaleLabel "Source Scale"
#define kParamSourceScaleHint "Scales the source image"

#define kParamAngleX "angleX"
#define kParamAngleXLabel "X Angle"
#define kParamAngleXHint "Rotation in degrees around the X angle"

#define kParamAngleY "angleY"
#define kParamAngleYLabel "Y Angle"
#define kParamAngleYHint "Rotation in degrees around the Y angle"

#define kParamAngleZ "angleZ"
#define kParamAngleZLabel "Z Angle"
#define kParamAngleZHint "Rotation in degrees around the Z angle"

#if defined(OFX_SUPPORTS_OPENGLRENDER) && defined(HAVE_OSMESA)
#define kParamUseGPU "useGPUIfAvailable"
#define kParamUseGPULabel "Use GPU If Available"
#define kParamUseGPUHint "If GPU rendering is available, use it. If the checkbox is not enabled, GPU rendering is not available on this host."
#endif

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */

TestOpenGLPlugin::TestOpenGLPlugin(OfxImageEffectHandle handle)
: ImageEffect(handle)
, _dstClip(0)
, _srcClip(0)
, _scale(0)
, _sourceScale(0)
, _angleX(0)
, _angleY(0)
, _angleZ(0)
, _useGPUIfAvailable(0)
{
    _dstClip = fetchClip(kOfxImageEffectOutputClipName);
    assert(_dstClip && (_dstClip->getPixelComponents() == OFX::ePixelComponentRGBA ||
                        _dstClip->getPixelComponents() == OFX::ePixelComponentAlpha));
    _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
    assert((!_srcClip && getContext() == OFX::eContextGenerator) ||
           (_srcClip && (_srcClip->getPixelComponents() == OFX::ePixelComponentRGBA ||
                         _srcClip->getPixelComponents() == OFX::ePixelComponentAlpha)));

    _scale = fetchDoubleParam(kParamScale);
    _sourceScale = fetchDoubleParam(kParamSourceScale);
    assert(_scale && _sourceScale);
    _angleX = fetchDoubleParam(kParamAngleX);
    _angleY = fetchDoubleParam(kParamAngleY);
    _angleZ = fetchDoubleParam(kParamAngleZ);
    assert(_angleX && _angleY && _angleZ);
#if defined(OFX_SUPPORTS_OPENGLRENDER) && defined(HAVE_OSMESA)
    _useGPUIfAvailable = fetchBooleanParam(kParamUseGPU);
    assert(_useGPUIfAvailable);
    const OFX::ImageEffectHostDescription &gHostDescription = *OFX::getImageEffectHostDescription();
    if (!gHostDescription.supportsOpenGLRender) {
        _useGPUIfAvailable->setEnabled(false);
    }
#endif

}


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

// the overridden render function
void
TestOpenGLPlugin::render(const OFX::RenderArguments &args)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    assert(kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth());

    bool openGLRender = false;
#if defined(OFX_SUPPORTS_OPENGLRENDER)
    const OFX::ImageEffectHostDescription &gHostDescription = *OFX::getImageEffectHostDescription();
    if (gHostDescription.supportsOpenGLRender) {
        _useGPUIfAvailable->getValueAtTime(args.time, openGLRender);
    }

    // do the rendering
    if (openGLRender) {
        return renderGL(args);
    }
#endif
#ifdef HAVE_OSMESA
    if (!openGLRender) {
        return renderMesa(args);
    }
#endif // HAVE_OSMESA
    OFX::throwSuiteStatusException(kOfxStatFailed);
}

// overriding getRegionOfDefinition is necessary to tell the host that we do not support render scale
bool
TestOpenGLPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &/*rod*/)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    // use the default RoD
    return false;
}

void
TestOpenGLPlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
    // We have to do this because the processing code does not support varying components for srcClip and dstClip
    // (The OFX spec doesn't state a default value for this)
    clipPreferences.setClipComponents(*_dstClip, _srcClip->getPixelComponents());
}

mDeclarePluginFactory(TestOpenGLPluginFactory, ;, {});

using namespace OFX;

void TestOpenGLPluginFactory::load()
{
    // we can't be used on hosts that don't support the stereoscopic suite
    // returning an error here causes a blank menu entry in Nuke
    //#if defined(OFX_SUPPORTS_OPENGLRENDER) && !defined(HAVE_OSMESA)
    //const ImageEffectHostDescription &gHostDescription = *OFX::getImageEffectHostDescription();
    //if (!gHostDescription.supportsOpenGLRender) {
    //    throwHostMissingSuiteException(kOfxOpenGLRenderSuite);
    //}
    //#endif
}

void
TestOpenGLPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // returning an error here crashes Nuke
    //#if defined(OFX_SUPPORTS_OPENGLRENDER) && !defined(HAVE_OSMESA)
    //const ImageEffectHostDescription &gHostDescription = *OFX::getImageEffectHostDescription();
    //if (!gHostDescription.supportsOpenGLRender) {
    //    throwHostMissingSuiteException(kOfxOpenGLRenderSuite);
    //}
    //#endif

    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add the supported contexts
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);

    // add supported pixel depths
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    // We can render both fields in a fielded images in one hit if there is no animation
    // So set the flag that allows us to do this
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    // say we can support multiple pixel depths and let the clip preferences action deal with it all.
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    // we support OpenGL rendering (could also say "needed" here)
#ifdef OFX_SUPPORTS_OPENGLRENDER
#ifdef HAVE_OSMESA
    desc.setSupportsOpenGLRender(true);
#else
    desc.setNeedsOpenGLRender(true);
    /*
     * If a host supports OpenGL rendering then it flags this with the string
     * property ::kOfxImageEffectOpenGLRenderSupported on its descriptor property
     * set. Effects that cannot run without OpenGL support should examine this in
     * ::kOfxActionDescribe action and return a ::kOfxStatErrMissingHostFeature
     * status flag if it is not set to "true".
     */
    const ImageEffectHostDescription &gHostDescription = *OFX::getImageEffectHostDescription();
    if (!gHostDescription.supportsOpenGLRender) {
        throwSuiteStatusException(kOfxStatErrMissingHostFeature);
    }
#endif
#endif
}

void
TestOpenGLPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/)
{
#if defined(OFX_SUPPORTS_OPENGLRENDER) && !defined(HAVE_OSMESA)
    const ImageEffectHostDescription &gHostDescription = *OFX::getImageEffectHostDescription();
    if (!gHostDescription.supportsOpenGLRender) {
        throwHostMissingSuiteException(kOfxOpenGLRenderSuite);
    }
#endif

    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamScale);
        param->setLabel(kParamScaleLabel);
        param->setHint(kParamScaleHint);
        // say we are a scaling parameter
        param->setDoubleType(eDoubleTypeScale);
        param->setDefault(1.);
        param->setRange(0., kOfxFlagInfiniteMax);
        param->setDisplayRange(0., 10.);
        param->setIncrement(0.01);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSourceScale);
        param->setLabel(kParamSourceScaleLabel);
        param->setHint(kParamSourceScaleHint);
        // say we are a scaling parameter
        param->setDoubleType(eDoubleTypeScale);
        param->setDefault(1.);
        param->setRange(0., kOfxFlagInfiniteMax);
        param->setDisplayRange(0., 10.);
        param->setIncrement(0.01);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamAngleX);
        param->setLabel(kParamAngleXLabel);
        param->setHint(kParamAngleXHint);
        // say we are a angle parameter
        param->setDoubleType(eDoubleTypeAngle);
        param->setDefault(0.);
        param->setRange(kOfxFlagInfiniteMin, kOfxFlagInfiniteMax);
        param->setDisplayRange(-180., 180.);
        param->setIncrement(1.);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamAngleY);
        param->setLabel(kParamAngleYLabel);
        param->setHint(kParamAngleYHint);
        // say we are a angle parameter
        param->setDoubleType(eDoubleTypeAngle);
        param->setDefault(0.);
        param->setRange(kOfxFlagInfiniteMin, kOfxFlagInfiniteMax);
        param->setDisplayRange(-180., 180.);
        param->setIncrement(1.);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamAngleZ);
        param->setLabel(kParamAngleZLabel);
        param->setHint(kParamAngleZHint);
        // say we are a angle parameter
        param->setDoubleType(eDoubleTypeAngle);
        param->setDefault(0.);
        param->setRange(kOfxFlagInfiniteMin, kOfxFlagInfiniteMax);
        param->setDisplayRange(-180., 180.);
        param->setIncrement(1.);
        if (page) {
            page->addChild(*param);
        }
    }

#if defined(OFX_SUPPORTS_OPENGLRENDER) && defined(HAVE_OSMESA)
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamUseGPU);
        param->setLabel(kParamUseGPULabel);
        param->setHint(kParamUseGPUHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }
#endif
}

OFX::ImageEffect*
TestOpenGLPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new TestOpenGLPlugin(handle);
}


void getTestOpenGLPluginID(OFX::PluginFactoryArray &ids)
{
    static TestOpenGLPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

#endif // OFX_SUPPORTS_OPENGLRENDER
