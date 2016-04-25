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
 * https://www.shadertoy.com (v0.8.3 as of march 22, 2016)
 * http://www.iquilezles.org/apps/shadertoy/index2.html (original Shader Toy v0.4)
 *
 * TODO:
 * - add RoD + extra parameters (see SeExpr)
 * - add multipass support (using tabs for UI as in shadertoys)
 */

#if defined(OFX_SUPPORTS_OPENGLRENDER) || defined(HAVE_OSMESA) // at least one is required for this plugin

#include "Shadertoy.h"

#include <cfloat>
#include <cstddef>
#include <climits>
#include <string>
#include <fstream>
#include <streambuf>

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <windows.h>
#endif

#include "ofxsImageEffect.h"
#include "ofxsMacros.h"
#include "ofxOpenGLRender.h"

using namespace OFX;

//OFXS_NAMESPACE_ANONYMOUS_ENTER // defines external classes
#define NBINPUTS SHADERTOY_NBINPUTS
#define NBUNIFORMS SHADERTOY_NBUNIFORMS

#define kPluginName "Shadertoy"
#define kPluginGrouping "Filter"
#define kPluginDescription \
"Apply a Shadertoy fragment shader (multipass shaders are not supported). See www.shadertoy.com\n" \
"\n" \
"This help only covers the parts of GLSL ES that are relevant for Shadertoy. " \
"For the complete specification please have a look at GLSL ES specification " \
"http://www.khronos.org/registry/gles/specs/2.0/GLSL_ES_Specification_1.0.17.pdf " \
"or pages 3 and 4 of the OpenGL ES 2.0 quick reference card " \
"https://www.khronos.org/opengles/sdk/docs/reference_cards/OpenGL-ES-2_0-Reference-card.pdf\n" \
"\n" \
"Language:\n" \
"\n" \
"    Preprocessor: # #define #undef #if #ifdef #ifndef #else #elif #endif #error #pragma #extension #version #line\n" \
/*"    Operators: () + - ! * / % < > <= >= == != && ||\n"*/ \
"    Operators: usual GLSL/C/C++/Java operators\n" \
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
"float	iChannelTime["STRINGISE(NBINPUTS)"]	image	Time for channel (if video or sound), in seconds\n" \
"vec3	iChannelResolution["STRINGISE(NBINPUTS)"]	image/sound	Input texture resolution for each channel\n" \
"vec4	iMouse	image	xy = current pixel coords (if LMB is down). zw = click pixel\n" \
"sampler2D	iChannel{i}	image/sound	Sampler for input textures i\n" \
"vec4	iDate	image/sound	Year, month, day, time in seconds in .xyzw\n" \
"float	iSampleRate	image/sound	The sound sample rate (typically 44100)\n" \
"vec4	iRenderScale	The OpenFX render scale (e.g. 0.5,0.5 when rendering half-size) [OFX plugin only]\n" \
"\n" \
"Shadertoy Outputs\n" \
"For image shaders, fragColor is used as output channel. It is not, for now, mandatory but recommended to leave the alpha channel to 1.0.\n" \
"\n" \
"For sound shaders, the mainSound() function returns a vec2 containing the left and right (stereo) sound channel wave data.\n" \
""


#define kPluginIdentifier "net.sf.openfx.Shadertoy"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
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
"uniform float     iChannelTime["STRINGISE(NBINPUTS)"];       // channel playback time (in seconds)\n" \
"uniform vec3      iChannelResolution["STRINGISE(NBINPUTS)"]; // channel resolution (in pixels)\n" \
"uniform vec4      iMouse;                // mouse pixel coords. xy: current (if MLB down), zw: click\n" \
"uniform samplerXX iChannel0..3;          // input channel. XX = 2D/Cube\n" \
"uniform vec4      iDate;                 // (year, month, day, time in seconds)\n" \
"uniform float     iSampleRate;           // sound sample rate (i.e., 44100)\n" \
""

#define kGroupImageShader "imageShaderGroup"
#define kGroupImageShaderLabel "Image Shader"

#define kParamImageShaderFileName "imageShaderFileName"
#define kParamImageShaderFileNameLabel "Load from File"
#define kParamImageShaderFileNameHint "Load the source from the given file. The file contents is only loaded once. Press the \"Reload\" button to load again the same file."

#define kParamImageShaderReload "imageShaderReload"
#define kParamImageShaderReloadLabel "Reload"
#define kParamImageShaderReloadHint "Reload the source from the given file."

#define kParamImageShaderSource "imageShaderSource"
#define kParamImageShaderSourceLabel "Source"
#define kParamImageShaderSourceHint "Image shader.\n\n"kShaderInputsHint

#define kParamImageShaderCompile "imageShaderCompile"
#define kParamImageShaderCompileLabel "Compile"
#define kParamImageShaderCompileHint "Compile the image shader."

#define kParamImageShaderTriggerRender "imageShaderTriggerRender"

#define kParamImageShaderDefault                            \
"void mainImage( out vec4 fragColor, in vec2 fragCoord )\n" \
"{\n"                                                       \
"    vec2 uv = fragCoord.xy / iResolution.xy;\n"            \
"    fragColor = vec4(uv,0.5+0.5*sin(iGlobalTime),1.0);\n"  \
"}"

// mouse parameters, see https://www.shadertoy.com/view/Mss3zH
#define kParamMousePosition "mousePosition"
#define kParamMousePositionLabel "Mouse Pos."
#define kParamMousePositionHint "Mouse position, in pixels. Gets mapped to the xy components of the iMouse input."

#define kParamMouseClick "mouseClick"
#define kParamMouseClickLabel "Click Pos."
#define kParamMouseClickHint "Mouse click position, in pixels. The zw components of the iMouse input contain mouseClick if mousePressed is checked, else -mouseClick."

#define kParamMousePressed "mousePressed"
#define kParamMousePressedLabel "Mouse Pressed"
#define kParamMousePressedHint "When checked, the zw components of the iMouse input contain mouseClick, else they contain -mouseClick. If the host does not support animating this parameter, use negative values for mouseClick to emulate a released mouse button."

#define kGroupExtraParameters "extraParametersGroup"
#define kGroupExtraParametersLabel "Extra Parameters"
#define kGroupExtraParametersHint "Description of extra parameters (a.k.a. uniforms) used by the shader source. Note that these parameters must be explicitely declared as uniforms in the shader (to keep compatibility with shadertoy, they may also have a default value set in the shader source)."

#define kParamCount "paramCount"
#define kParamCountLabel "No. of Params"
#define kParamCountHint "Number of extra parameters."

#define kParamType "paramType" // followed by param number
#define kParamTypeLabel1 "Param "
#define kParamTypeLabel2 " Type"
#define kParamTypeHint "Type of the parameter."
#define kParamTypeOptionNone "none"
#define kParamTypeOptionBool "bool"
#define kParamTypeOptionInt "int"
#define kParamTypeOptionFloat "float"
#define kParamTypeOptionVec2 "vec2"
#define kParamTypeOptionVec3 "vec3"
#define kParamTypeOptionVec4 "vec4"

#define kParamName "paramName" // followed by param number
#define kParamNameLabel "Name"
#define kParamNameHint "Name of the parameter, as used in the shader."

#define kParamValueBool "paramValueBool" // followed by param number
#define kParamValueInt "paramValueInt" // followed by param number
#define kParamValueFloat "paramValueFloat" // followed by param number
#define kParamValueVec2 "paramValueVec2" // followed by param number
#define kParamValueVec3 "paramValueVec3" // followed by param number
#define kParamValueVec4 "paramValueVec4" // followed by param number
#define kParamValueLabel "Value" // followed by param number
#define kParamValueHint "Value of the parameter."

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

#define kParamRendererInfo "rendererInfo"
#define kParamRendererInfoLabel "Renderer Info..."
#define kParamRendererInfoHint "Retrieve information about the current OpenGL renderer."

#define kClipChannel "iChannel"

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */

ShadertoyPlugin::ShadertoyPlugin(OfxImageEffectHandle handle)
: ImageEffect(handle)
, _dstClip(0)
, _srcClips(NBINPUTS, (OFX::Clip*)NULL)
, _imageShaderFileName(0)
, _imageShaderSource(0)
, _imageShaderCompile(0)
, _imageShaderTriggerRender(0)
, _paramCount(0)
, _paramType      (NBUNIFORMS, (OFX::ChoiceParam*)  NULL)
, _paramName      (NBUNIFORMS, (OFX::StringParam*)  NULL)
, _paramValueBool (NBUNIFORMS, (OFX::BooleanParam*) NULL)
, _paramValueInt  (NBUNIFORMS, (OFX::IntParam*)     NULL)
, _paramValueFloat(NBUNIFORMS, (OFX::DoubleParam*)  NULL)
, _paramValueVec2 (NBUNIFORMS, (Double2DParam*)     NULL)
, _paramValueVec3 (NBUNIFORMS, (OFX::Double3DParam*)NULL)
, _paramValueVec4 (NBUNIFORMS, (OFX::RGBAParam*)    NULL)
, _mipmap(0)
, _anisotropic(0)
, _useGPUIfAvailable(0)
, _haveAniso(false)
, _maxAnisoMax(1.)
#ifdef HAVE_OSMESA
, _imageShaderID(1)
, _imageShaderUniformsID(1)
#endif
, _imageShader(0)
, _imageShaderChanged(true)
, _imageShaderUniformsChanged(true)
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
    for (unsigned i = 0; i < NBINPUTS; ++i) {
        assert((!_srcClips[i] && getContext() == OFX::eContextGenerator) ||
               (_srcClips[i] && (_srcClips[i]->getPixelComponents() == OFX::ePixelComponentRGBA ||
                             _srcClips[i]->getPixelComponents() == OFX::ePixelComponentAlpha)));
    }

    _imageShaderFileName = fetchStringParam(kParamImageShaderFileName);
    _imageShaderSource = fetchStringParam(kParamImageShaderSource);
    _imageShaderCompile = fetchPushButtonParam(kParamImageShaderCompile);
    _imageShaderTriggerRender = fetchIntParam(kParamImageShaderTriggerRender);
    assert(_imageShaderFileName && _imageShaderSource && _imageShaderCompile && _imageShaderTriggerRender);
    _mousePosition = fetchDouble2DParam(kParamMousePosition);
    _mouseClick = fetchDouble2DParam(kParamMouseClick);
    _mousePressed = fetchBooleanParam(kParamMousePressed);
    assert(_mousePosition && _mousePressed && _mouseClick);
    _paramCount = fetchIntParam(kParamCount);
    assert(_paramCount);
    for (unsigned i = 0; i < NBUNIFORMS; ++i) {
        std::string nb;
        // generate the number string
        for (unsigned j = i + 1; j !=0; j /= 10) {
            nb += ('0' + (j % 10));
        }
        _paramType[i]       = fetchChoiceParam  (std::string(kParamType) + nb);
        _paramName[i]       = fetchStringParam  (std::string(kParamName) + nb);
        _paramValueBool[i]  = fetchBooleanParam (std::string(kParamValueBool)  + nb);
        _paramValueInt[i]   = fetchIntParam     (std::string(kParamValueInt)   + nb);
        _paramValueFloat[i] = fetchDoubleParam  (std::string(kParamValueFloat) + nb);
        _paramValueVec2[i]  = fetchDouble2DParam(std::string(kParamValueVec2)  + nb);
        _paramValueVec3[i]  = fetchDouble3DParam(std::string(kParamValueVec3)  + nb);
        _paramValueVec4[i]  = fetchRGBAParam    (std::string(kParamValueVec4)  + nb);
        assert(_paramType[i] && _paramName[i] && _paramValueBool[i] && _paramValueInt[i] && _paramValueFloat[i] && _paramValueVec2[i] && _paramValueVec3[i] && _paramValueVec4[i]);
    }
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
    updateVisibility();
    initOpenGL();
#if defined(HAVE_OSMESA)
    initMesa();
#endif
    _imageShaderCompile->setEnabled(false); // always compile on first render
}


ShadertoyPlugin::~ShadertoyPlugin()
{
    exitOpenGL();
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
ShadertoyPlugin::render(const OFX::RenderArguments &args)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    for (unsigned i = 0; i < NBINPUTS; ++i) {
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
ShadertoyPlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args,
                                      OFX::RegionOfInterestSetter &rois)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    // The effect requires full images to render any region
    for (unsigned i = 0; i < NBINPUTS; ++i) {
        OfxRectD srcRoI;

        if (_srcClips[i] && _srcClips[i]->isConnected()) {
            srcRoI = _srcClips[i]->getRegionOfDefinition(args.time);
            rois.setRegionOfInterest(*_srcClips[i], srcRoI);
        }
    }
}

void
ShadertoyPlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
    // We have to do this because the processing code does not support varying components for srcClip and dstClip
    // (The OFX spec doesn't state a default value for this)
    if (_srcClips[0]) {
        clipPreferences.setClipComponents(*_dstClip, _srcClips[0]->getPixelComponents());
    }
    clipPreferences.setOutputFrameVarying(true);
    clipPreferences.setOutputHasContinousSamples(true);
}

static inline
bool
starts_with(const std::string &str, const std::string &prefix)
{
    return (str.substr(0, prefix.size()) == prefix);
}

void
ShadertoyPlugin::updateVisibility()
{
    unsigned paramCount = std::max(0, std::min(_paramCount->getValue(), NBUNIFORMS));
    for (unsigned i = 0; i < NBUNIFORMS; ++i) {
        updateVisibilityParam(i, i < paramCount);
    }
}

void
ShadertoyPlugin::updateVisibilityParam(unsigned i, bool visible)
{
    UniformTypeEnum paramType = (UniformTypeEnum)_paramType[i]->getValue();
    bool isBool = false;
    bool isInt = false;
    bool isFloat = false;
    bool isVec2 = false;
    bool isVec3 = false;
    bool isVec4 = false;

    if (visible) {
        switch (paramType) {
            case eUniformTypeNone: {
                break;
            }
            case eUniformTypeBool: {
                isBool = true;
                break;
            }
            case eUniformTypeInt: {
                isInt = true;
                break;
            }
            case eUniformTypeFloat: {
                isFloat = true;
                break;
            }
            case eUniformTypeVec2: {
                isVec2 = true;
                break;
            }
            case eUniformTypeVec3: {
                isVec3 = true;
                break;
            }
            case eUniformTypeVec4: {
                isVec4 = true;
                break;
            }
            default: {
                break;
            }
        }
    }

    _paramType[i]->setIsSecret(!visible);
    _paramName[i]->setIsSecret(!visible);
    _paramValueBool[i]->setIsSecret(!isBool);
    _paramValueInt[i]->setIsSecret(!isInt);
    _paramValueFloat[i]->setIsSecret(!isFloat);
    _paramValueVec2[i]->setIsSecret(!isVec2);
    _paramValueVec3[i]->setIsSecret(!isVec3);
    _paramValueVec4[i]->setIsSecret(!isVec4);
}

void
ShadertoyPlugin::changedParam(const OFX::InstanceChangedArgs &args,
                              const std::string &paramName)
{
    if (paramName == kParamImageShaderFileName ||
        paramName == kParamImageShaderReload) {
        // load image shader from file
        std::string imageShaderFileName;
        _imageShaderFileName->getValue(imageShaderFileName);
        if (!imageShaderFileName.empty()) {
            std::ifstream t(imageShaderFileName.c_str());
            if (t.bad()) {
                sendMessage(OFX::Message::eMessageError, "", std::string("Error: Cannot open file ")+imageShaderFileName);
            } else {
                std::string str;
                t.seekg(0, std::ios::end);
                str.reserve(t.tellg());
                t.seekg(0, std::ios::beg);
                str.assign((std::istreambuf_iterator<char>(t)),
                           std::istreambuf_iterator<char>());
                _imageShaderSource->setValue(str);
            }
        }

    } else if ((paramName == kParamImageShaderSource && args.reason != eChangeUserEdit) ||
               (paramName == kParamImageShaderCompile)) {
        {
            OFX::MultiThread::AutoMutex lock(_shaderMutex);
            // mark that image shader must be recompiled on next render
#         ifdef HAVE_OSMESA
            ++_imageShaderID;
#         endif
            _imageShaderChanged = true;
        }
        _imageShaderCompile->setEnabled(false);
        // trigger a new render
        clearPersistentMessage();
        _imageShaderTriggerRender->setValue(_imageShaderTriggerRender->getValue()+1);

    } else if (paramName == kParamImageShaderSource && args.reason == eChangeUserEdit) {
        _imageShaderCompile->setEnabled(true);

    } else if (paramName == kParamCount || starts_with(paramName, kParamName)) {
        {
            OFX::MultiThread::AutoMutex lock(_shaderMutex);
            // mark that image shader must be recompiled on next render
#         ifdef HAVE_OSMESA
            ++_imageShaderUniformsID;
#         endif
            _imageShaderUniformsChanged = true;
        }
        updateVisibility();

    } else if (starts_with(paramName, kParamType)) {
        {
            OFX::MultiThread::AutoMutex lock(_shaderMutex);
            // mark that image shader must be recompiled on next render
#         ifdef HAVE_OSMESA
            ++_imageShaderUniformsID;
#         endif
            _imageShaderUniformsChanged = true;
        }
        //updateVisibilityParam(i, i < paramCount);
        updateVisibility();

    } else if (paramName == kParamImageShaderSource && args.reason == eChangeUserEdit) {
        _imageShaderCompile->setEnabled(true);
    } else if (paramName == kParamRendererInfo) {
        const OFX::ImageEffectHostDescription &gHostDescription = *OFX::getImageEffectHostDescription();
        bool openGLRender = false;
        std::string message;
#     if defined(OFX_SUPPORTS_OPENGLRENDER)
        if (gHostDescription.supportsOpenGLRender) {
            _useGPUIfAvailable->getValueAtTime(args.time, openGLRender);
        }

        if (openGLRender) {
            OFX::MultiThread::AutoMutex lock(_rendererInfoMutex);
            message = _rendererInfoGL;
        }
#     endif
#     ifdef HAVE_OSMESA
        if (!openGLRender) {
            OFX::MultiThread::AutoMutex lock(_rendererInfoMutex);
            message = _rendererInfoMesa;
        }
#     endif // HAVE_OSMESA
        if (message.empty()) {
            sendMessage(OFX::Message::eMessageMessage, "", "OpenGL renderer info not yet available.\n"
                        "Please execute at least one image render and try again.");
        } else {
            sendMessage(OFX::Message::eMessageMessage, "", message);
        }
    }
}


mDeclarePluginFactory(ShadertoyPluginFactory, ;, {});


void ShadertoyPluginFactory::load()
{
    // we can't be used on hosts that don't support the OpenGL suite
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
    char iChannelX[10] = "iChannelX"; // index 8 holds the channel character
    assert(NBINPUTS < 10 && iChannelX[8] == 'X');
    {
        iChannelX[8] = '0';
        ClipDescriptor *srcClip = desc.defineClip((context == eContextFilter) ?
                                                  kOfxImageEffectSimpleSourceClipName :
                                                  iChannelX);
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentAlpha);
        srcClip->setTemporalClipAccess(false);
        srcClip->setSupportsTiles(kSupportsTiles);
        srcClip->setIsMask(false);
        srcClip->setOptional(!(context == eContextFilter));
    }
    for (unsigned i = 1; i < NBINPUTS; ++i ){
        iChannelX[8] = '0' + i;
        ClipDescriptor *srcClip = desc.defineClip(iChannelX);
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
            //group->setAsTab();
        }

        {
            OFX::StringParamDescriptor* param = desc.defineStringParam(kParamImageShaderFileName);
            param->setLabel(kParamImageShaderFileNameLabel);
            param->setHint(kParamImageShaderFileNameHint);
            param->setStringType(eStringTypeFilePath);
            param->setFilePathExists(true);
            param->setLayoutHint(eLayoutHintNoNewLine, 1);
            param->setAnimates(false);
            if (page) {
                page->addChild(*param);
            }
            if (group) {
                param->setParent(*group);
            }
        }

        {
            OFX::PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamImageShaderReload);
            param->setLabel(kParamImageShaderReloadLabel);
            param->setHint(kParamImageShaderReloadHint);
            if (page) {
                page->addChild(*param);
            }
            if (group) {
                param->setParent(*group);
            }
        }

        {
            OFX::StringParamDescriptor* param = desc.defineStringParam(kParamImageShaderSource);
            param->setLabel(kParamImageShaderSourceLabel);
            param->setHint(kParamImageShaderSourceHint);
            param->setStringType(eStringTypeMultiLine);
            param->setDefault(kParamImageShaderDefault);
            param->setEvaluateOnChange(false); // render is triggered using kParamImageShaderTriggerRender
            param->setAnimates(false);
            if (page) {
                page->addChild(*param);
            }
            if (group) {
                param->setParent(*group);
            }
        }

        {
            OFX::PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamImageShaderCompile);
            param->setLabel(kParamImageShaderCompileLabel);
            param->setHint(kParamImageShaderCompileHint);
            if (page) {
                page->addChild(*param);
            }
            if (group) {
                param->setParent(*group);
            }
        }

        {
            // a dummy boolean parameter, used to trigger a new render when the shader is recompiled
            OFX::IntParamDescriptor* param = desc.defineIntParam(kParamImageShaderTriggerRender);
            param->setEvaluateOnChange(true);
            param->setAnimates(false);
            param->setIsSecret(true);
            param->setIsPersistant(false);
            if (page) {
                page->addChild(*param);
            }
            if (group) {
                param->setParent(*group);
            }
        }

        if (page) {
            page->addChild(*group);
        }
    }

    {
        OFX::Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamMousePosition);
        param->setLabel(kParamMousePositionLabel);
        param->setHint(kParamMousePositionHint);
        param->setDoubleType(eDoubleTypeXYAbsolute);
        param->setUseHostNativeOverlayHandle(true);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamMouseClick);
        param->setLabel(kParamMouseClickLabel);
        param->setHint(kParamMouseClickHint);
        param->setDoubleType(eDoubleTypeXYAbsolute);
        param->setUseHostNativeOverlayHandle(true);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamMousePressed);
        param->setLabel(kParamMousePressedLabel);
        param->setHint(kParamMousePressedHint);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::GroupParamDescriptor* group = desc.defineGroupParam(kGroupExtraParameters);
        if (group) {
            group->setLabel(kGroupExtraParametersLabel);
            group->setHint(kGroupExtraParametersHint);
            group->setOpen(false);
        }

        {
            OFX::IntParamDescriptor* param = desc.defineIntParam(kParamCount);
            param->setLabel(kParamCountLabel);
            param->setHint(kParamCountHint);
            param->setRange(0, NBUNIFORMS);
            param->setDisplayRange(0, NBUNIFORMS);
            param->setAnimates(false);
            if (page) {
                page->addChild(*param);
            }
            if (group) {
                param->setParent(*group);
            }
        }

        for (unsigned i = 0; i < NBUNIFORMS; ++i) {
            std::string nb;
            // generate the number string
            for (unsigned j = i + 1; j !=0; j /= 10) {
                nb += ('0' + (j % 10));
            }
            {
                OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(std::string(kParamType) + nb);
                param->setLabel(std::string(kParamTypeLabel1) + nb + kParamTypeLabel2);
                param->setHint(kParamTypeHint);
                assert(param->getNOptions() == ShadertoyPlugin::eUniformTypeNone);
                param->appendOption(kParamTypeOptionNone);
                assert(param->getNOptions() == ShadertoyPlugin::eUniformTypeBool);
                param->appendOption(kParamTypeOptionBool);
                assert(param->getNOptions() == ShadertoyPlugin::eUniformTypeInt);
                param->appendOption(kParamTypeOptionInt);
                assert(param->getNOptions() == ShadertoyPlugin::eUniformTypeFloat);
                param->appendOption(kParamTypeOptionFloat);
                assert(param->getNOptions() == ShadertoyPlugin::eUniformTypeVec2);
                param->appendOption(kParamTypeOptionVec2);
                assert(param->getNOptions() == ShadertoyPlugin::eUniformTypeVec3);
                param->appendOption(kParamTypeOptionVec3);
                assert(param->getNOptions() == ShadertoyPlugin::eUniformTypeVec4);
                param->appendOption(kParamTypeOptionVec4);
                param->setEvaluateOnChange(true);
                param->setAnimates(false);
                param->setLayoutHint(eLayoutHintNoNewLine, 1);
                if (page) {
                    page->addChild(*param);
                }
                if (group) {
                    param->setParent(*group);
                }
            }
            {
                OFX::StringParamDescriptor* param = desc.defineStringParam(std::string(kParamName) + nb);
                param->setLabel(kParamNameLabel);
                param->setHint(kParamNameHint);
                param->setEvaluateOnChange(true);
                param->setAnimates(false);
                if (page) {
                    page->addChild(*param);
                }
                if (group) {
                    param->setParent(*group);
                }
            }
            {
                OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(std::string(kParamValueBool) + nb);
                param->setLabel(kParamValueLabel);
                param->setHint(kParamValueHint);
                param->setEvaluateOnChange(true);
                param->setAnimates(true);
                if (page) {
                    page->addChild(*param);
                }
                if (group) {
                    param->setParent(*group);
                }
            }
            {
                OFX::IntParamDescriptor* param = desc.defineIntParam(std::string(kParamValueInt) + nb);
                param->setLabel(kParamValueLabel);
                param->setHint(kParamValueHint);
                param->setRange(INT_MIN, INT_MAX);
                param->setDisplayRange(INT_MIN, INT_MAX);
                param->setEvaluateOnChange(true);
                param->setAnimates(true);
                if (page) {
                    page->addChild(*param);
                }
                if (group) {
                    param->setParent(*group);
                }
            }
            {
                OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(std::string(kParamValueFloat) + nb);
                param->setLabel(kParamValueLabel);
                param->setHint(kParamValueHint);
                param->setRange(-DBL_MAX, DBL_MAX);
                param->setDisplayRange(-DBL_MAX, DBL_MAX);
                param->setEvaluateOnChange(true);
                param->setAnimates(true);
                if (page) {
                    page->addChild(*param);
                }
                if (group) {
                    param->setParent(*group);
                }
            }
            {
                OFX::Double2DParamDescriptor* param = desc.defineDouble2DParam(std::string(kParamValueVec2) + nb);
                param->setLabel(kParamValueLabel);
                param->setHint(kParamValueHint);
                param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX);
                param->setDisplayRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX);
                param->setDimensionLabels("x", "y");
                param->setEvaluateOnChange(true);
                param->setAnimates(true);
                if (page) {
                    page->addChild(*param);
                }
                if (group) {
                    param->setParent(*group);
                }
            }
            {
                OFX::Double3DParamDescriptor* param = desc.defineDouble3DParam(std::string(kParamValueVec3) + nb);
                param->setLabel(kParamValueLabel);
                param->setHint(kParamValueHint);
                param->setRange(-DBL_MAX, -DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX);
                param->setDisplayRange(-DBL_MAX, -DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX);
                param->setDimensionLabels("x", "y", "z");
                param->setEvaluateOnChange(true);
                param->setAnimates(true);
                if (page) {
                    page->addChild(*param);
                }
                if (group) {
                    param->setParent(*group);
                }
            }
            {
                OFX::RGBAParamDescriptor* param = desc.defineRGBAParam(std::string(kParamValueVec4) + nb);
                param->setLabel(kParamValueLabel);
                param->setHint(kParamValueHint);
                param->setRange(-DBL_MAX, -DBL_MAX, -DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX);
                param->setDisplayRange(-DBL_MAX, -DBL_MAX, -DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX);
                param->setDimensionLabels("x", "y", "z", "w");
                param->setEvaluateOnChange(true);
                param->setAnimates(true);
                if (page) {
                    page->addChild(*param);
                }
                if (group) {
                    param->setParent(*group);
                }
            }

            if (page) {
                page->addChild(*group);
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

    {
        OFX::PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamRendererInfo);
        param->setLabel(kParamRendererInfoLabel);
        param->setHint(kParamRendererInfoHint);
        if (page) {
            page->addChild(*param);
        }
    }
}

OFX::ImageEffect*
ShadertoyPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new ShadertoyPlugin(handle);
}


static ShadertoyPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

#endif // OFX_SUPPORTS_OPENGLRENDER
