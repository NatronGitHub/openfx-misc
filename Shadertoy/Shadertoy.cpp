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
 *
 * References:
 * https://www.shadertoy.com
 * http://www.iquilezles.org/apps/shadertoy/index2.html (original Shader Toy v0.4)
 *
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
"Apply shaders from www.shadertoy.com.\n" \
"\n" \
"This help only covers the parts of GLSL ES that are relevant for Shadertoy. " \
"For the complete specification please have a look at GLSL ES specification " \
"<http://www.khronos.org/registry/gles/specs/2.0/GLSL_ES_Specification_1.0.17.pdf>\n" \
"Language:\n" \
"\n" \
"    Preprocessor: # #define #undef #if #ifdef #ifndef #else #elif #endif #error #pragma #extension #version #line\n" \
"    Operators: () + - ! * / % < > <= >= == != && ||\n" \
"    Comments: // /* */\n" \
"    Types: void bool int float vec2 vec3 vec4 bvec2 bvec3 bvec4 ivec2 ivec3 ivec4 mat2 mat3 mat4 sampler2D\n" \
"    Function Parameter Qualifiers: [none], in, out, inout\n" \
"    Global Variable Qualifiers: const\n" \
"    Vector Components: .xyzw .rgba .stpq\n" \
"    Flow Control: if else for return break continue\n" \
"    Output: vec4 fragColor\n" \
"    Input: vec2 fragCoord\n" \
"\n" \
"\n" \
"Built-in Functions:\n" \
"\n" \
"    type radians (type degrees)\n" \
"    type degrees (type radians)\n" \
"    type sin (type angle)\n" \
"    type cos (type angle)\n" \
"    type tan (type angle)\n" \
"    type asin (type x)\n" \
"    type acos (type x)\n" \
"    type atan (type y, type x)\n" \
"    type atan (type y_over_x)\n" \
"\n" \
"	\n" \
"\n" \
"    type pow (type x, type y)\n" \
"    type exp (type x)\n" \
"    type log (type x)\n" \
"    type exp2 (type x)\n" \
"    type log2 (type x)\n" \
"    type sqrt (type x)\n" \
"    type inversesqrt (type x)\n" \
"\n" \
"    type abs (type x)\n" \
"    type sign (type x)\n" \
"    type floor (type x)\n" \
"    type ceil (type x)\n" \
"    type fract (type x)\n" \
"    type mod (type x, float y)\n" \
"    type mod (type x, type y)\n" \
"    type min (type x, type y)\n" \
"    type min (type x, float y)\n" \
"    type max (type x, type y)\n" \
"    type max (type x, float y)\n" \
"    type clamp (type x, type minV, type maxV)\n" \
"    type clamp (type x, float minV, float maxV)\n" \
"    type mix (type x, type y, type a)\n" \
"    type mix (type x, type y, float a)\n" \
"    type step (type edge, type x)\n" \
"    type step (float edge, type x)\n" \
"    type smoothstep (type a, type b, type x)\n" \
"    type smoothstep (float a, float b, type x)\n" \
"    mat matrixCompMult (mat x, mat y)\n" \
"\n" \
"	\n" \
"\n" \
"    float length (type x)\n" \
"    float distance (type p0, type p1)\n" \
"    float dot (type x, type y)\n" \
"    vec3 cross (vec3 x, vec3 y)\n" \
"    type normalize (type x)\n" \
"    type faceforward (type N, type I, type Nref)\n" \
"    type reflect (type I, type N)\n" \
"    type refract (type I, type N,float eta)\n" \
"\n" \
"    bvec lessThan(vec x, vec y)\n" \
"    bvec lessThan(ivec x, ivec y)\n" \
"    bvec lessThanEqual(vec x, vec y)\n" \
"    bvec lessThanEqual(ivec x, ivec y)\n" \
"    bvec greaterThan(vec x, vec y)\n" \
"    bvec greaterThan(ivec x, ivec y)\n" \
"    bvec greaterThanEqual(vec x, vec y)\n" \
"    bvec greaterThanEqual(ivec x, ivec y)\n" \
"    bvec equal(vec x, vec y)\n" \
"    bvec equal(ivec x, ivec y)\n" \
"    bvec equal(bvec x, bvec y)\n" \
"    bvec notEqual(vec x, vec y)\n" \
"    bvec notEqual(ivec x, ivec y)\n" \
"    bvec notEqual(bvec x, bvec y)\n" \
"    bool any(bvec x)\n" \
"    bool all(bvec x)\n" \
"    bvec not(bvec x)\n" \
"\n" \
"	\n" \
"\n" \
"    vec4 texture2D(sampler2D sampler, vec2 coord )\n" \
"    vec4 texture2D(sampler2D sampler, vec2 coord, float bias)\n" \
"    vec4 textureCube(samplerCube sampler, vec3 coord)\n" \
"    vec4 texture2DProj(sampler2D sampler, vec3 coord )\n" \
"    vec4 texture2DProj(sampler2D sampler, vec3 coord, float bias)\n" \
"    vec4 texture2DProj(sampler2D sampler, vec4 coord)\n" \
"    vec4 texture2DProj(sampler2D sampler, vec4 coord, float bias)\n" \
"    vec4 texture2DLodEXT(sampler2D sampler, vec2 coord, float lod)\n" \
"    vec4 texture2DProjLodEXT(sampler2D sampler, vec3 coord, float lod)\n" \
"    vec4 texture2DProjLodEXT(sampler2D sampler, vec4 coord, float lod)\n" \
"    vec4 textureCubeLodEXT(samplerCube sampler, vec3 coord, float lod)\n" \
"    vec4 texture2DGradEXT(sampler2D sampler, vec2 P, vec2 dPdx, vec2 dPdy)\n" \
"    vec4 texture2DProjGradEXT(sampler2D sampler, vec3 P, vec2 dPdx, vec2 dPdy)\n" \
"    vec4 texture2DProjGradEXT(sampler2D sampler, vec4 P, vec2 dPdx, vec2 dPdy)\n" \
"    vec4 textureCubeGradEXT(samplerCube sampler, vec3 P, vec3 dPdx, vec3 dPdy)\n" \
"\n" \
"    type dFdx( type x ), dFdy( type x )\n" \
"    type fwidth( type p )\n" \
"\n" \
"\n" \
"How-to\n" \
"\n" \
"    Use structs: struct myDataType { float occlusion; vec3 color; }; myDataType myData = myDataType(0.7, vec3(1.0, 2.0, 3.0));\n" \
"    Initialize arrays: arrays cannot be initialized in WebGL.\n" \
"    Do conversions: int a = 3; float b = float(a);\n" \
"    Do component swizzling: vec4 a = vec4(1.0,2.0,3.0,4.0); vec4 b = a.zyyw;\n" \
"    Access matrix components: mat4 m; m[1] = vec4(2.0); m[0][0] = 1.0; m[2][3] = 2.0;\n" \
"\n" \
"\n" \
"Be careful!\n" \
"\n" \
"    the f suffix for floating pont numbers: 1.0f is illegal in GLSL. You must use 1.0\n" \
"    saturate(): saturate(x) doesn't exist in GLSL. Use clamp(x,0.0,1.0) instead\n" \
"    pow/sqrt: please don't feed sqrt() and pow() with negative numbers. Add an abs() or max(0.0,) to the argument\n" \
"    mod: please don't do mod(x,0.0). This is undefined in some platforms\n" \
"    variables: initialize your variables! Don't assume they'll be set to zero by default\n" \
"    functions: don't call your functions the same as some of your variables\n" \
"\n" \
"\n" \
"Shadertoy Inputs\n" \
"vec3	iResolution	image	The viewport resolution (z is pixel aspect ratio, usually 1.0)\n" \
"float	iGlobalTime	image/sound	Current time in seconds\n" \
"float	iTimeDelta	image	Time it takes to render a frame, in seconds\n" \
"int	iFrame	image	Current frame\n" \
"float	iFrameRate	image	Number of frames rendered per second\n" \
"float	iChannelTime[4]	image	Time for channel (if video or sound), in seconds\n" \
"vec3	iChannelResolution[4]	image/sound	Input texture resolution for each channel\n" \
"vec4	iMouse	image	xy = current pixel coords (if LMB is down). zw = click pixel\n" \
"sampler2D	iChannel{i}	image/sound	Sampler for input textures i\n" \
"vec4	iDate	image/sound	Year, month, day, time in seconds in .xyzw\n" \
"float	iSampleRate	image/sound	The sound sample rate (typically 44100)\n" \
"\n" \
"Shadertoy Outputs\n" \
"For image shaders, fragColor is used as output channel. It is not, for now, mandatory but recommended to leave the alpha channel to 1.0.\n" \
"\n" \
"For sound shaders, the mainSound() function returns a vec2 containing the left and right (stereo) sound channel wave data.\n" \
""


#define kPluginIdentifier "net.sf.openfx.Shadertoy"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 0
#define kSupportsMultiResolution 0
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe


#define kShaderInputsHint \
"Shader Inputs:\n" \
"uniform vec3      iResolution;           // viewport resolution (in pixels)\n" \
"uniform float     iGlobalTime;           // shader playback time (in seconds)\n" \
"uniform float     iTimeDelta;            // render time (in seconds)\n" \
"uniform int       iFrame;                // shader playback frame\n" \
"uniform float     iChannelTime[4];       // channel playback time (in seconds)\n" \
"uniform vec3      iChannelResolution[4]; // channel resolution (in pixels)\n" \
"uniform vec4      iMouse;                // mouse pixel coords. xy: current (if MLB down), zw: click\n" \
"uniform samplerXX iChannel0..3;          // input channel. XX = 2D/Cube\n" \
"uniform vec4      iDate;                 // (year, month, day, time in seconds)\n" \
"uniform float     iSampleRate;           // sound sample rate (i.e., 44100)\n" \
""

#define kGroupImageShader "imageShaderGroup"
#define kGroupImageShaderLabel "Image"

#define kParamImageShaderSource "imageShaderSource"
#define kParamImageShaderSourceLabel "Source"
#define kParamImageShaderSourceHint "Image shader.\n\n"kShaderInputsHint


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
        case OFX::eContextFilter:
            _srcClips[0] = fetchClip(kOfxImageEffectSimpleSourceClipName);
            _srcClips[1] = fetchClip(kClipChannel"1");
            _srcClips[2] = fetchClip(kClipChannel"2");
            _srcClips[3] = fetchClip(kClipChannel"3");
            break;
        case OFX::eContextGenerator:
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

    _imageShader = fetchStringParam(kParamImageShaderSource);
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
    desc.addSupportedContext(eContextGenerator);
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
ShadertoyPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
#if defined(OFX_SUPPORTS_OPENGLRENDER) && !defined(HAVE_OSMESA)
    const ImageEffectHostDescription &gHostDescription = *OFX::getImageEffectHostDescription();
    if (!gHostDescription.supportsOpenGLRender) {
        throwHostMissingSuiteException(kOfxOpenGLRenderSuite);
    }
#endif

    // Source clip only in the filter context
    // create the mandated source clip
    if (context == eContextFilter) {
        ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentAlpha);
        srcClip->setTemporalClipAccess(false);
        srcClip->setSupportsTiles(kSupportsTiles);
        srcClip->setIsMask(false);
        srcClip->setOptional(false);
    } else {
        ClipDescriptor *srcClip = desc.defineClip(kClipChannel"0");
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentAlpha);
        srcClip->setTemporalClipAccess(false);
        srcClip->setSupportsTiles(kSupportsTiles);
        srcClip->setIsMask(false);
        srcClip->setOptional(true);
    }
    {
        ClipDescriptor *srcClip = desc.defineClip(kClipChannel"1");
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentAlpha);
        srcClip->setTemporalClipAccess(false);
        srcClip->setSupportsTiles(kSupportsTiles);
        srcClip->setIsMask(false);
        srcClip->setOptional(true);
    }
    {
        ClipDescriptor *srcClip = desc.defineClip(kClipChannel"2");
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentAlpha);
        srcClip->setTemporalClipAccess(false);
        srcClip->setSupportsTiles(kSupportsTiles);
        srcClip->setIsMask(false);
        srcClip->setOptional(true);
    }
    {
        ClipDescriptor *srcClip = desc.defineClip(kClipChannel"3");
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentAlpha);
        srcClip->setTemporalClipAccess(false);
        srcClip->setSupportsTiles(kSupportsTiles);
        srcClip->setIsMask(false);
        srcClip->setOptional(true);
    }
    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        OFX::GroupParamDescriptor* group = desc.defineGroupParam(kGroupImageShader);
        if (group) {
            group->setLabel(kGroupImageShaderLabel);
            group->setAsTab();
        }

        {
            OFX::StringParamDescriptor* param = desc.defineStringParam(kParamImageShaderSource);
            param->setLabel(kParamImageShaderSourceLabel);
            param->setHint(kParamImageShaderSourceHint);
            param->setStringType(eStringTypeMultiLine);
            param->setDefault(kParamImageShaderDefault);
            if (page) {
                page->addChild(*param);
            }
            if (group) {
                param->setParent(*group);
            }
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
