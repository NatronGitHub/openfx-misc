/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2016 INRIA
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
 * OFX Shadertoy plugin.
 * TODO:
 * - actual shadertoy render (for now, it is a placeholder)
 * - only recompile shader if it changed
 * - for each shader, add a "From file" checkbox and a "File" param - this disables the shader param.
 * - when unchecking the checkbox, the "File" param is still enabled, and the shader param is enabled too (so it can be edited)
 * - at plugin initialization, if the "From file" checkbox is checked, reload the shader from the file
 * - add a "Save file as" non-persistant param to save the edited shader
 * - add multipass support (using tabs for UI as in shadertoys)
 */

#if defined(OFX_SUPPORTS_OPENGLRENDER) || defined(HAVE_OSMESA) // at least one is required for this plugin

#include "Shadertoy.h"

#include <cfloat>
#include <cstddef>

#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsImageEffect.h"
#include "ofxsMacros.h"
#include "ofxOpenGLRender.h"

using namespace OFX;

//OFXS_NAMESPACE_ANONYMOUS_ENTER // defines external classes

#define kPluginName "Shadertoy"
#define kPluginGrouping "Filter"
#define kPluginDescription \
"Apply shaders from www.shadertoy.com."

#define kPluginIdentifier "net.sf.openfx.Shadertoy"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 0
#define kSupportsMultiResolution 0
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamImageShader "imageShader"
#define kParamImageShaderLabel "Image"
#define kParamImageShaderHint "Image shader"
#define kParamImageShaderDefault                            \
"void mainImage( out vec4 fragColor, in vec2 fragCoord )\n" \
"{\n"                                                       \
"    vec2 uv = fragCoord.xy / iResolution.xy;\n"            \
"    fragColor = vec4(uv,0.5+0.5*sin(iGlobalTime),1.0);\n"  \
"}"

#define kParamMipmap "mipmap"
#define kParamMipmapLabel "Mipmap"
#define kParamMipmapHint "Use mipmapping (if supported)"

#define kParamAnisotropic "anisotropic"
#define kParamAnisotropicLabel "Anisotropic"
#define kParamAnisotropicHint "Use anisotropic texture filtering (if supported)"

#if defined(OFX_SUPPORTS_OPENGLRENDER) && defined(HAVE_OSMESA)
#define kParamUseGPU "useGPUIfAvailable"
#define kParamUseGPULabel "Use GPU If Available"
#define kParamUseGPUHint "If GPU rendering is available, use it. If the checkbox is not enabled, GPU rendering is not available on this host."
#endif

#define kClipChannel "iChannel"

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */

ShadertoyPlugin::ShadertoyPlugin(OfxImageEffectHandle handle)
: ImageEffect(handle)
, _dstClip(0)
, _srcClips(4, NULL)
, _imageShader(0)
, _mipmap(0)
, _anisotropic(0)
, _useGPUIfAvailable(0)
, _haveAniso(false)
, _maxAnisoMax(1.)
{
    _dstClip = fetchClip(kOfxImageEffectOutputClipName);
    assert(_dstClip && (_dstClip->getPixelComponents() == OFX::ePixelComponentRGBA ||
                        _dstClip->getPixelComponents() == OFX::ePixelComponentAlpha));
    switch (getContext()) {
        case OFX::eContextGenerator:
            break;
        case OFX::eContextFilter:
            _srcClips[0] = fetchClip(kOfxImageEffectSimpleSourceClipName);
            _srcClips[1] = fetchClip(kClipChannel"1");
            _srcClips[2] = fetchClip(kClipChannel"2");
            _srcClips[3] = fetchClip(kClipChannel"3");
            break;
        case OFX::eContextGeneral:
        default:
            _srcClips[0] = fetchClip(kClipChannel"0");
            _srcClips[1] = fetchClip(kClipChannel"1");
            _srcClips[2] = fetchClip(kClipChannel"2");
            _srcClips[3] = fetchClip(kClipChannel"3");
            break;
    }
    for (unsigned i = 0; i < 4; ++i) {
        assert((!_srcClips[i] && getContext() == OFX::eContextGenerator) ||
               (_srcClips[i] && (_srcClips[i]->getPixelComponents() == OFX::ePixelComponentRGBA ||
                             _srcClips[i]->getPixelComponents() == OFX::ePixelComponentAlpha)));
    }

    _imageShader = fetchStringParam(kParamImageShader);
    assert(_imageShader);
    _mipmap = fetchBooleanParam(kParamMipmap);
    _anisotropic = fetchBooleanParam(kParamAnisotropic);
    assert(_mipmap && _anisotropic);
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
ShadertoyPlugin::render(const OFX::RenderArguments &args)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    for (unsigned i = 0; i < 4; ++i) {
        assert(kSupportsMultipleClipPARs   || !_srcClips[i] || _srcClips[i]->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
        assert(kSupportsMultipleClipDepths || !_srcClips[i] || _srcClips[i]->getPixelDepth()       == _dstClip->getPixelDepth());
    }

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
ShadertoyPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &/*rod*/)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    // use the default RoD
    return false;
}

void
ShadertoyPlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
    // We have to do this because the processing code does not support varying components for srcClip and dstClip
    // (The OFX spec doesn't state a default value for this)
    if (_srcClips[0]) {
        clipPreferences.setClipComponents(*_dstClip, _srcClips[0]->getPixelComponents());
    }
}

mDeclarePluginFactory(ShadertoyPluginFactory, ;, {});


void ShadertoyPluginFactory::load()
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
ShadertoyPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
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
    desc.setSupportsRenderQuality(true);

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
ShadertoyPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/)
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
        OFX::StringParamDescriptor* param = desc.defineStringParam(kParamImageShader);
        param->setLabel(kParamImageShaderLabel);
        param->setHint(kParamImageShaderHint);
        param->setStringType(eStringTypeMultiLine);
        param->setDefault(kParamImageShaderDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamMipmap);
        param->setLabel(kParamMipmapLabel);
        param->setHint(kParamMipmapHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamAnisotropic);
        param->setLabel(kParamAnisotropicLabel);
        param->setHint(kParamAnisotropicHint);
        param->setDefault(true);
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
ShadertoyPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new ShadertoyPlugin(handle);
}


static ShadertoyPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

#endif // OFX_SUPPORTS_OPENGLRENDER
