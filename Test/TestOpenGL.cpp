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
 * OFX TestOpenGL plugin.
 */

#if defined(OFX_SUPPORTS_OPENGLRENDER) || defined(HAVE_OSMESA) // at least one is required for this plugin

#include "TestOpenGL.h"

#include <cfloat>

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <windows.h>
#endif

#include "ofxsImageEffect.h"
#include "ofxsMacros.h"
#include "ofxOpenGLRender.h"

using namespace OFX;

//OFXS_NAMESPACE_ANONYMOUS_ENTER // defines external classes

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

#define kParamSourceStretch "sourceStretch"
#define kParamSourceStretchLabel "Source Stretch"
#define kParamSourceStretchHint "Stretches the source image"

#define kParamTeapotScale "teapotScale"
#define kParamTeapotScaleLabel "Teapot Scale"
#define kParamTeapotScaleHint "Scales the teapot"


#define kParamAngleX "angleX"
#define kParamAngleXLabel "X Angle"
#define kParamAngleXHint "Rotation in degrees around the X angle"

#define kParamAngleY "angleY"
#define kParamAngleYLabel "Y Angle"
#define kParamAngleYHint "Rotation in degrees around the Y angle"

#define kParamAngleZ "angleZ"
#define kParamAngleZLabel "Z Angle"
#define kParamAngleZHint "Rotation in degrees around the Z angle"

#define kParamProjective "projective"
#define kParamProjectiveLabel "Projective"
#define kParamProjectiveHint "Use projective texture mapping (effect is noticeable if stretch is nonzero)"

#define kParamMipmap "mipmap"
#define kParamMipmapLabel "Mipmap"
#define kParamMipmapHint "Use mipmapping (available only with CPU rendering)"

#define kParamAnisotropic "anisotropic"
#define kParamAnisotropicLabel "Anisotropic"
#define kParamAnisotropicHint "Use anisotropic texture filtering (available with CPU rendering, and with GPU if supported)"

#if defined(OFX_SUPPORTS_OPENGLRENDER) && defined(HAVE_OSMESA)
#define kParamEnableGPU "enableGPU"
#define kParamEnableGPULabel "Enable GPU Render"
#define kParamEnableGPUHint \
    "Enable GPU-based OpenGL render.\n" \
    "If the checkbox is checked but is not enabled (i.e. it cannot be unchecked), GPU render can not be enabled or disabled from the plugin and is probably part of the host options.\n" \
    "If the checkbox is not checked and is not enabled (i.e. it cannot be checked), GPU render is not available on this host.\n"
#endif

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */

TestOpenGLPlugin::TestOpenGLPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    , _scale(0)
    , _sourceScale(0)
    , _sourceStretch(0)
    , _teapotScale(0)
    , _angleX(0)
    , _angleY(0)
    , _angleZ(0)
    , _projective(0)
    , _mipmap(0)
    , _anisotropic(0)
    , _enableGPU(0)
    , _openGLContextData()
    , _openGLContextAttached(false)
{
    _dstClip = fetchClip(kOfxImageEffectOutputClipName);
    assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == OFX::ePixelComponentRGBA ||
                         _dstClip->getPixelComponents() == OFX::ePixelComponentAlpha) );
    _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
    assert( (!_srcClip && getContext() == OFX::eContextGenerator) ||
            ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  OFX::ePixelComponentRGBA ||
                           _srcClip->getPixelComponents() == OFX::ePixelComponentAlpha) ) );

    _scale = fetchDouble2DParam(kParamScale);
    _sourceScale = fetchDouble2DParam(kParamSourceScale);
    _sourceStretch = fetchDoubleParam(kParamSourceStretch);
    _teapotScale = fetchDoubleParam(kParamTeapotScale);
    assert(_scale && _sourceScale);
    _angleX = fetchDoubleParam(kParamAngleX);
    _angleY = fetchDoubleParam(kParamAngleY);
    _angleZ = fetchDoubleParam(kParamAngleZ);
    assert(_angleX && _angleY && _angleZ);
    _projective = fetchBooleanParam(kParamProjective);
    _mipmap = fetchBooleanParam(kParamMipmap);
    _anisotropic = fetchBooleanParam(kParamAnisotropic);
    assert(_projective && _mipmap && _anisotropic);
#if defined(OFX_SUPPORTS_OPENGLRENDER) && defined(HAVE_OSMESA)
    _enableGPU = fetchBooleanParam(kParamEnableGPU);
    assert(_enableGPU);
    const OFX::ImageEffectHostDescription &gHostDescription = *OFX::getImageEffectHostDescription();
    if (!gHostDescription.supportsOpenGLRender) {
        _enableGPU->setEnabled(false);
        setSupportsOpenGLRender(false);
    } else {
        setSupportsOpenGLRender( _enableGPU->getValue() );
    }
#endif
#if defined(HAVE_OSMESA)
    initMesa();
#endif
}

TestOpenGLPlugin::~TestOpenGLPlugin()
{
#if defined(HAVE_OSMESA)
    exitMesa();
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
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );

    bool openGLRender = false;
#if defined(OFX_SUPPORTS_OPENGLRENDER)
    openGLRender = args.openGLEnabled;

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
TestOpenGLPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args,
                                        OfxRectD & /*rod*/)
{
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
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
    clipPreferences.setClipComponents( *_dstClip, _srcClip->getPixelComponents() );
}

void
TestOpenGLPlugin::changedParam(const OFX::InstanceChangedArgs &args,
                               const std::string &paramName)
{
#if defined(HAVE_OSMESA)
    if (paramName == kParamEnableGPU) {
        setSupportsOpenGLRender( _enableGPU->getValueAtTime(args.time) );
    }
#endif
} // TestOpenGLPlugin::changedParam

mDeclarePluginFactory(TestOpenGLPluginFactory,; , {});
void
TestOpenGLPluginFactory::load()
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

    desc.setRenderThreadSafety(kRenderThreadSafety);
} // TestOpenGLPluginFactory::describe

void
TestOpenGLPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                           OFX::ContextEnum /*context*/)
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
        OFX::Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamScale);
        param->setLabel(kParamScaleLabel);
        param->setHint(kParamScaleHint);
        // say we are a scaling parameter
        param->setDoubleType(eDoubleTypeScale);
        param->setDefault(1., 1.);
        param->setRange(0., 0., DBL_MAX, DBL_MAX);
        param->setDisplayRange(0., 0., 10., 10.);
        param->setIncrement(0.01);
        param->setUseHostNativeOverlayHandle(false);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        OFX::Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamSourceScale);
        param->setLabel(kParamSourceScaleLabel);
        param->setHint(kParamSourceScaleHint);
        // say we are a scaling parameter
        param->setDoubleType(eDoubleTypeScale);
        param->setDefault(1., 1.);
        param->setRange(0., 0., DBL_MAX, DBL_MAX);
        param->setDisplayRange(0., 0., 10., 10.);
        param->setIncrement(0.01);
        param->setUseHostNativeOverlayHandle(false);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSourceStretch);
        param->setLabel(kParamSourceStretchLabel);
        param->setHint(kParamSourceStretchHint);
        param->setDefault(0.);
        param->setRange(0., 0.999);
        param->setDisplayRange(0., 1.);
        param->setIncrement(0.01);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamTeapotScale);
        param->setLabel(kParamTeapotScaleLabel);
        param->setHint(kParamTeapotScaleHint);
        // say we are a scaling parameter
        param->setDoubleType(eDoubleTypeScale);
        param->setDefault(1.);
        param->setRange(0., DBL_MAX);
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
        param->setRange(-DBL_MAX, DBL_MAX);
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
        param->setRange(-DBL_MAX, DBL_MAX);
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
        param->setRange(-DBL_MAX, DBL_MAX);
        param->setDisplayRange(-180., 180.);
        param->setIncrement(1.);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProjective);
        param->setLabel(kParamProjectiveLabel);
        param->setHint(kParamProjectiveHint);
        param->setDefault(true);
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
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamEnableGPU);
        param->setLabel(kParamEnableGPULabel);
        param->setHint(kParamEnableGPUHint);
        const OFX::ImageEffectHostDescription &gHostDescription = *OFX::getImageEffectHostDescription();
        // Resolve advertises OpenGL support in its host description, but never calls render with OpenGL enabled
        if ( gHostDescription.supportsOpenGLRender && (gHostDescription.hostName != "DaVinciResolveLite") ) {
            param->setDefault(true);
            if (gHostDescription.APIVersionMajor * 100 + gHostDescription.APIVersionMinor < 104) {
                // Switching OpenGL render from the plugin was introduced in OFX 1.4
                param->setEnabled(false);
            }
        } else {
            param->setDefault(false);
            param->setEnabled(false);
        }

        if (page) {
            page->addChild(*param);
        }
    }
#endif
} // TestOpenGLPluginFactory::describeInContext

OFX::ImageEffect*
TestOpenGLPluginFactory::createInstance(OfxImageEffectHandle handle,
                                        OFX::ContextEnum /*context*/)
{
    return new TestOpenGLPlugin(handle);
}

static TestOpenGLPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

#endif // OFX_SUPPORTS_OPENGLRENDER
