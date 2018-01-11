/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2018 INRIA
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
 * https://www.shadertoy.com (v0.8.8 https://www.shadertoy.com/changelog)
 * http://www.iquilezles.org/apps/shadertoy/index2.html (original Shader Toy v0.4)
 * https://shadertoyunofficial.wordpress.com/2016/07/22/compatibility-issues-in-shadertoy-webglsl/#webgl2
 *
 * TODO:
 * - upgrade to Shaderttoy 0.9.1:
 *   - support WebGL 2.0 / OpenGL ES 3.0
 *     (https://www.khronos.org/registry/OpenGL/specs/es/3.0/GLSL_ES_Specification_3.00.pdf
 *      and pages 4 and 5 of
 *      https://www.khronos.org/files/opengles3-quick-reference-card.pdf )
 *      GLSL 3.30 https://www.khronos.org/registry/OpenGL/specs/gl/GLSLangSpec.3.30.pdf
 *      Note that this probably means we have to switch to an OpenGL Core profile,
 *      so the host must give us an OpenGL Core context.
 *      See also: https://shadertoyunofficial.wordpress.com/2017/02/16/webgl-2-0-vs-webgl-1-0/
 * - add multipass support (using tabs for UI as in shadertoys)
 * - synthclipse-compatible comments http://synthclipse.sourceforge.net/user_guide/fragx/commands.html
 * - use .stoy for the presets shaders, and add the default shadertoy uniforms at the beginning, as in http://synthclipse.sourceforge.net/user_guide/shadertoy.html
 * - ShaderToy export as in synthclipse http://synthclipse.sourceforge.net/user_guide/shadertoy.html
 */

#if defined(OFX_SUPPORTS_OPENGLRENDER) || defined(HAVE_OSMESA) // at least one is required for this plugin

#include "Shadertoy.h"

#include <cfloat> // DBL_MAX
#include <cstddef>
#include <climits>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <string>
#include <fstream>
#include <streambuf>
#ifdef DEBUG
#include <iostream>
#endif

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <windows.h>
#endif

#include "ofxsImageEffect.h"
#include "ofxsMacros.h"
#include "ofxOpenGLRender.h"
#include "ofxsCoords.h"
#include "ofxsFormatResolution.h"

using namespace OFX;

using std::string;

//OFXS_NAMESPACE_ANONYMOUS_ENTER // defines external classes
#define NBINPUTS SHADERTOY_NBINPUTS
#define NBUNIFORMS SHADERTOY_NBUNIFORMS

#define kPluginName "Shadertoy"
#define kPluginGrouping "Filter"
#define kPluginDescription \
    "Apply a Shadertoy fragment shader. See http://www.shadertoy.com\n" \
    "\n" \
    "This plugin implements Shadertoy 0.8.8, but multipass shaders and sound are not supported.\n" \
    "\n" \
    "Shadertoy 0.8.8 uses WebGL 1.0 (a.k.a. GLSL ES 1.0 from GLES 2.0), based on GLSL 1.20.\n" \
    "\n" \
    "Note that the more recent Shadertoy 0.9.1 uses WebGL 2.0 (a.k.a. GLSL ES 3.0 from GLES 3.0), based on GLSL 3.3.\n" \
    "\n" \
    "This help only covers the parts of GLSL ES that are relevant for Shadertoy. For the complete specification please have a look at GLSL ES 1.0 specification https://www.khronos.org/registry/OpenGL/specs/es/2.0/GLSL_ES_Specification_1.00.pdf or pages 3 and 4 of the OpenGL ES 2.0 quick reference card https://www.khronos.org/opengles/sdk/docs/reference_cards/OpenGL-ES-2_0-Reference-card.pdf\n" \
    "A Shadertoy/GLSL tutorial can be found at https://www.shadertoy.com/view/Md23DV\n" \
    "\n" \
    "Image shaders\n" \
    "\n" \
    "Image shaders implement the `mainImage()` function in order to generate the procedural images by computing a color for each pixel. This function is expected to be called once per pixel, and it is responsability of the host application to provide the right inputs to it and get the output color from it and assign it to the screen pixel. The prototype is:\n" \
    "\n" \
    "`void mainImage( out vec4 fragColor, in vec2 fragCoord );`\n" \
    "\n" \
    "where `fragCoord` contains the pixel coordinates for which the shader needs to compute a color. The coordinates are in pixel units, ranging from 0.5 to resolution-0.5, over the rendering surface, where the resolution is passed to the shader through the `iResolution` uniform (see below).\n" \
    "\n" \
    "The resulting color is gathered in `fragColor` as a four component vector.\n" \
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
    "Built-in Functions (see http://www.shaderific.com/glsl-functions/ for details):\n" \
    "\n" \
    "Angle and Trigonometry Functions\n" \
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
    "Exponential Functions\n" \
    "    type pow (type x, type y)\n" \
    "    type exp (type x)\n" \
    "    type log (type x)\n" \
    "    type exp2 (type x)\n" \
    "    type log2 (type x)\n" \
    "    type sqrt (type x)\n" \
    "    type inversesqrt (type x)\n" \
    "\n" \
    "Common Functions\n" \
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
    "\n" \
    "Geometric Functions\n" \
    "    float length (type x)\n" \
    "    float distance (type p0, type p1)\n" \
    "    float dot (type x, type y)\n" \
    "    vec3 cross (vec3 x, vec3 y)\n" \
    "    type normalize (type x)\n" \
    "    type faceforward (type N, type I, type Nref)\n" \
    "    type reflect (type I, type N)\n" \
    "    type refract (type I, type N,float eta)\n" \
    "\n" \
    "Matrix Functions\n" \
    "    mat matrixCompMult (mat x, mat y)\n" \
    "\n" \
    "Vector Relational Functions\n" \
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
    "Texture Lookup Functions\n" \
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
    "Function Derivatives\n" \
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
    "float	iTime	image/sound	Current time in seconds\n" \
    "float	iTimeDelta	image	Time it takes to render a frame, in seconds\n" \
    "int	iFrame	image	Current frame\n" \
    "float	iFrameRate	image	Number of frames rendered per second\n" \
    "float	iChannelTime[" STRINGISE(NBINPUTS) "]	image	Time for channel (if video or sound), in seconds\n" \
    "vec3	iChannelResolution[" STRINGISE(NBINPUTS) "]	image/sound	Input texture resolution for each channel\n" \
    "vec2   iChannelOffset[" STRINGISE(NBINPUTS) "]   image   Input texture offset in pixel coords for each channel\n" \
    "vec4	iMouse	image	xy = current pixel coords (if LMB is down). zw = click pixel\n" \
    "sampler2D	iChannel{i}	image/sound	Sampler for input textures i\n" \
    "vec4	iDate	image/sound	Year, month, day, time in seconds in .xyzw\n" \
    "float	iSampleRate	image/sound	The sound sample rate (typically 44100)\n" \
    "vec2	iRenderScale	image	The OpenFX render scale (e.g. 0.5,0.5 when rendering half-size) [OFX plugin only]\n" \
    "\n" \
    "Shadertoy Outputs\n" \
    "For image shaders, fragColor is used as output channel. It is not, for now, mandatory but recommended to leave the alpha channel to 1.0.\n" \
    "\n" \
    "For sound shaders, the mainSound() function returns a vec2 containing the left and right (stereo) sound channel wave data.\n" \
    "\n" \
    "OpenFX extensions to Shadertoy\n" \
    "\n" \
    "* The pre-defined `iRenderScale` uniform contains the current render scale. Basically all pixel sizes must be multiplied by the renderscale to get a scale-independent effect. For compatibility with Shadertoy, the first line that starts with `const vec2 iRenderScale` is ignored (the full line should be `const vec2 iRenderScale = vec2(1.,1.);`).\n" \
    "* The pre-defined `iChannelOffset` uniform contains the texture offset for each channel relative to channel 0. For compatibility with Shadertoy, the first line that starts with `const vec2 iChannelOffset` is ignored (the full line should be `const vec2 iChannelOffset[4] = vec2[4]( vec2(0.,0.), vec2(0.,0.), vec2(0.,0.), vec2(0.,0.) );`).\n" \
    "* The shader may define additional uniforms, which should have a default value, as in `uniform vec2 blurSize = vec2(5., 5.);`.\n" \
    "  These uniforms can be made available as OpenFX parameters using settings in the 'Extra parameters' group, which can be set automatically using the 'Auto. Params' button (in this case, parameters are updated when the image is rendered).\n" \
    "  A parameter label and help string can be given in the comment on the same line. The help string must be in parenthesis.\n" \
    "  `uniform vec2 blurSize = vec2(5., 5.); // Blur Size (The blur size in pixels.)`\n" \
    "  min/max values can also be given after a comma. The strings must be exactly `min=` and `max=`, without additional spaces, separated by a comma, and the values must have the same dimension as the uniform:\n" \
    "  `uniform vec2 blurSize = vec2(5., 5.); // Blur Size (The blur size in pixels.), min=(0.,0.), max=(1000.,1000.)`\n" \
    "* The following comment line placed in the shader gives a label and help string to input 1 (the comment must be the only thing on the line):\n" \
    "  `// iChannel1: Noise (A noise texture to be used for random number calculations. The texture should not be frame-varying.)`\n" \
    "* This one also sets the filter and wrap parameters:\n" \
    "  `// iChannel0: Source (Source image.), filter=linear, wrap=clamp`\n" \
    "* And this one sets the output bouding box (possible values are Default, Union, Intersection, and iChannel0 to iChannel3):\n" \
    "  `// BBox: iChannel0`\n" \
    "\n" \
    "\n" \
    "Default textures and videos\n" \
    "\n" \
    "The default shadertoy textures and videos are avalaible from the Shadertoy web site. In order to mimic the behavior of each shader, download the corresponding textures or videos and connect them to the proper input.\n" \
    "\n" \
    "- Textures: https://www.shadertoy.com/presets/tex00.jpg https://www.shadertoy.com/presets/tex01.jpg https://www.shadertoy.com/presets/tex02.jpg https://www.shadertoy.com/presets/tex03.jpg https://www.shadertoy.com/presets/tex04.jpg https://www.shadertoy.com/presets/tex05.jpg https://www.shadertoy.com/presets/tex06.jpg https://www.shadertoy.com/presets/tex07.jpg https://www.shadertoy.com/presets/tex08.jpg https://www.shadertoy.com/presets/tex09.jpg https://www.shadertoy.com/presets/tex10.png https://www.shadertoy.com/presets/tex11.png https://www.shadertoy.com/presets/tex12.png ttps://www.shadertoy.com/presets/tex14.png https://www.shadertoy.com/presets/tex15.png https://www.shadertoy.com/presets/tex16.png https://www.shadertoy.com/presets/tex17.jpg https://www.shadertoy.com/presets/tex18.jpg https://www.shadertoy.com/presets/tex19.png https://www.shadertoy.com/presets/tex20.jpg https://www.shadertoy.com/presets/tex21.png\n" \
    "- Videos: https://www.shadertoy.com/presets/vid00.ogv https://www.shadertoy.com/presets/vid01.webm https://www.shadertoy.com/presets/vid02.ogv https://www.shadertoy.com/presets/vid03.webm\n" \
    "- Cubemaps: https://www.shadertoy.com/presets/cube00_0.jpg https://www.shadertoy.com/presets/cube01_0.png https://www.shadertoy.com/presets/cube02_0.jpg https://www.shadertoy.com/presets/cube03_0.png https://www.shadertoy.com/presets/cube04_0.png https://www.shadertoy.com/presets/cube05_0.png" \



#define kPluginDescriptionMarkdown \
    "Apply a [Shadertoy](http://www.shadertoy.com) fragment shader.\n" \
    "\n" \
    "This plugin implements [Shadertoy 0.8.8](https://www.shadertoy.com/changelog), but multipass shaders and sound are not supported.\n" \
    "\n" \
    "[Shadertoy 0.8.8](https://www.shadertoy.com/changelog) uses WebGL 1.0 (a.k.a. [GLSL ES 1.0](https://www.khronos.org/registry/OpenGL/specs/es/2.0/GLSL_ES_Specification_1.00.pdf) from GLES 2.0), based on [GLSL 1.20](https://www.khronos.org/registry/OpenGL/specs/gl/GLSLangSpec.1.20.pdf)\n" \
    "\n" \
    "Note that the more recent [Shadertoy 0.9.1](https://www.shadertoy.com/changelog) uses WebGL 2.0 (a.k.a. [GLSL ES 3.0](https://www.khronos.org/registry/OpenGL/specs/es/3.0/GLSL_ES_Specification_3.00.pdf) from GLES 3.0), based on [GLSL 3.3](https://www.khronos.org/registry/OpenGL/specs/gl/GLSLangSpec.3.30.pdf)\n" \
    "\n" \
    "This help only covers the parts of GLSL ES that are relevant for Shadertoy. For the complete specification please have a look at [GLSL ES 1.0 specification](https://www.khronos.org/registry/OpenGL/specs/es/2.0/GLSL_ES_Specification_1.00.pdf) or pages 3 and 4 of the [OpenGL ES 2.0 quick reference card](https://www.khronos.org/opengles/sdk/docs/reference_cards/OpenGL-ES-2_0-Reference-card.pdf).\n" \
    "See also the [Shadertoy/GLSL tutorial](https://www.shadertoy.com/view/Md23DV).\n" \
    "\n" \
    "### Image shaders\n" \
    "\n" \
    "Image shaders implement the `mainImage()` function in order to generate the procedural images by computing a color for each pixel. This function is expected to be called once per pixel, and it is responsability of the host application to provide the right inputs to it and get the output color from it and assign it to the screen pixel. The prototype is:\n" \
    "\n" \
    "`void mainImage( out vec4 fragColor, in vec2 fragCoord );`\n" \
    "\n" \
    "where `fragCoord` contains the pixel coordinates for which the shader needs to compute a color. The coordinates are in pixel units, ranging from 0.5 to resolution-0.5, over the rendering surface, where the resolution is passed to the shader through the `iResolution` uniform (see below).\n" \
    "\n" \
    "The resulting color is gathered in `fragColor` as a four component vector.\n" \
    "\n" \
    "### Language:\n" \
    "\n" \
    "* __Preprocessor:__ `#` `#define` `#undef` `#if` `#ifdef` `#ifndef` `#else` `#elif` `#endif` `#error` `#pragma` `#extension` `#version` `#line`\n" \
    "* __Operators:__ `()` `+` `-` `!` `*` `/` `%` `<` `>` `<=` `>=` `==` `!=` `&&` `||`\n" \
    "* __Comments:__ `//` `/*` `*/`\n" \
    "* __Types:__ void bool int float vec2 vec3 vec4 bvec2 bvec3 bvec4 ivec2 ivec3 ivec4 mat2 mat3 mat4 sampler2D\n" \
    "* __Function Parameter Qualifiers:__ ~~none~~, in, out, inout\n" \
    "* __Global Variable Qualifiers:__ const\n" \
    "* __Vector Components:__ .xyzw .rgba .stpq\n" \
    "* __Flow Control:__ if else for return break continue\n" \
    "* __Output:__ vec4 fragColor\n" \
    "* __Input:__ vec2 fragCoord\n" \
    "\n" \
    "\n" \
    "### Built-in Functions [(details)](http://www.shaderific.com/glsl-functions/)\n" \
    "\n" \
    "#### Angle and Trigonometry Functions\n" \
    "\n" \
    "* *type* radians (*type* degrees)\n" \
    "* *type* degrees (*type* radians)\n" \
    "* *type* sin (*type* angle)\n" \
    "* *type* cos (*type* angle)\n" \
    "* *type* tan (*type* angle)\n" \
    "* *type* asin (*type* x)\n" \
    "* *type* acos (*type* x)\n" \
    "* *type* atan (*type* y, *type* x)\n" \
    "* *type* atan (*type* y_over_x)\n" \
    "\n" \
    "#### Exponential Functions\n" \
    "\n" \
    "* *type* pow (*type* x, *type* y)\n" \
    "* *type* exp (*type* x)\n" \
    "* *type* log (*type* x)\n" \
    "* *type* exp2 (*type* x)\n" \
    "* *type* log2 (*type* x)\n" \
    "* *type* sqrt (*type* x)\n" \
    "* *type* inversesqrt (*type* x)\n" \
    "\n" \
    "#### Common Functions\n" \
    "\n" \
    "* *type* abs (*type* x)\n" \
    "* *type* sign (*type* x)\n" \
    "* *type* floor (*type* x)\n" \
    "* *type* ceil (*type* x)\n" \
    "* *type* fract (*type* x)\n" \
    "* *type* mod (*type* x, float y)\n" \
    "* *type* mod (*type* x, *type* y)\n" \
    "* *type* min (*type* x, *type* y)\n" \
    "* *type* min (*type* x, float y)\n" \
    "* *type* max (*type* x, *type* y)\n" \
    "* *type* max (*type* x, float y)\n" \
    "* *type* clamp (*type* x, *type* minV, *type* maxV)\n" \
    "* *type* clamp (*type* x, float minV, float maxV)\n" \
    "* *type* mix (*type* x, *type* y, *type* a)\n" \
    "* *type* mix (*type* x, *type* y, float a)\n" \
    "* *type* step (*type* edge, *type* x)\n" \
    "* *type* step (float edge, *type* x)\n" \
    "* *type* smoothstep (*type* a, *type* b, *type* x)\n" \
    "* *type* smoothstep (float a, float b, *type* x)\n" \
    "\n" \
    "#### Geometric Functions\n" \
    "\n" \
    "* float length (*type* x)\n" \
    "* float distance (*type* p0, *type* p1)\n" \
    "* float dot (*type* x, *type* y)\n" \
    "* vec3 cross (vec3 x, vec3 y)\n" \
    "* *type* normalize (*type* x)\n" \
    "* *type* faceforward (*type* N, *type* I, *type* Nref)\n" \
    "* *type* reflect (*type* I, *type* N)\n" \
    "* *type* refract (*type* I, *type* N,float eta)\n" \
    "\n" \
    "#### Matrix Functions\n" \
    "\n" \
    "* mat matrixCompMult (mat x, mat y)\n" \
    "\n" \
    "#### Vector Relational Functions\n" \
    "\n" \
    "* bvec lessThan(vec x, vec y)\n" \
    "* bvec lessThan(ivec x, ivec y)\n" \
    "* bvec lessThanEqual(vec x, vec y)\n" \
    "* bvec lessThanEqual(ivec x, ivec y)\n" \
    "* bvec greaterThan(vec x, vec y)\n" \
    "* bvec greaterThan(ivec x, ivec y)\n" \
    "* bvec greaterThanEqual(vec x, vec y)\n" \
    "* bvec greaterThanEqual(ivec x, ivec y)\n" \
    "* bvec equal(vec x, vec y)\n" \
    "* bvec equal(ivec x, ivec y)\n" \
    "* bvec equal(bvec x, bvec y)\n" \
    "* bvec notEqual(vec x, vec y)\n" \
    "* bvec notEqual(ivec x, ivec y)\n" \
    "* bvec notEqual(bvec x, bvec y)\n" \
    "* bool any(bvec x)\n" \
    "* bool all(bvec x)\n" \
    "* bvec not(bvec x)\n" \
    "\n" \
    "#### Texture Lookup Functions\n" \
    "\n" \
    "* vec4 texture2D(sampler2D sampler, vec2 coord )\n" \
    "* vec4 texture2D(sampler2D sampler, vec2 coord, float bias)\n" \
    "* vec4 textureCube(samplerCube sampler, vec3 coord)\n" \
    "* vec4 texture2DProj(sampler2D sampler, vec3 coord )\n" \
    "* vec4 texture2DProj(sampler2D sampler, vec3 coord, float bias)\n" \
    "* vec4 texture2DProj(sampler2D sampler, vec4 coord)\n" \
    "* vec4 texture2DProj(sampler2D sampler, vec4 coord, float bias)\n" \
    "* vec4 texture2DLodEXT(sampler2D sampler, vec2 coord, float lod)\n" \
    "* vec4 texture2DProjLodEXT(sampler2D sampler, vec3 coord, float lod)\n" \
    "* vec4 texture2DProjLodEXT(sampler2D sampler, vec4 coord, float lod)\n" \
    "* vec4 textureCubeLodEXT(samplerCube sampler, vec3 coord, float lod)\n" \
    "* vec4 texture2DGradEXT(sampler2D sampler, vec2 P, vec2 dPdx, vec2 dPdy)\n" \
    "* vec4 texture2DProjGradEXT(sampler2D sampler, vec3 P, vec2 dPdx, vec2 dPdy)\n" \
    "* vec4 texture2DProjGradEXT(sampler2D sampler, vec4 P, vec2 dPdx, vec2 dPdy)\n" \
    "* vec4 textureCubeGradEXT(samplerCube sampler, vec3 P, vec3 dPdx, vec3 dPdy)\n" \
    "\n" \
    "#### Function Derivatives\n" \
    "\n" \
    "* *type* dFdx( *type* x ), dFdy( *type* x )\n" \
    "* *type* fwidth( *type* p )\n" \
    "\n" \
    "\n" \
    "### How-to\n" \
    "\n" \
    "* __Use structs:__ `struct myDataType { float occlusion; vec3 color; }; myDataType myData = myDataType(0.7, vec3(1.0, 2.0, 3.0));`\n" \
    "* __Initialize arrays:__ arrays cannot be initialized in WebGL.\n" \
    "* __Do conversions:__ `int a = 3; float b = float(a);`\n" \
    "* __Do component swizzling:__ `vec4 a = vec4(1.0,2.0,3.0,4.0); vec4 b = a.zyyw;`\n" \
    "* __Access matrix components:__ `mat4 m; m[1] = vec4(2.0); m[0][0] = 1.0; m[2][3] = 2.0;`\n" \
    "\n" \
    "\n" \
    "### Be careful!\n" \
    "\n" \
    "* __the f suffix for floating point numbers:__ 1.0f is illegal in GLSL. You must use 1.0\n" \
    "* __saturate():__ saturate(x) doesn't exist in GLSL. Use clamp(x,0.0,1.0) instead\n" \
    "* __pow/sqrt:__ please don't feed sqrt() and pow() with negative numbers. Add an abs() or max(0.0,) to the argument\n" \
    "* __mod:__ please don't do mod(x,0.0). This is undefined in some platforms\n" \
    "* __variables:__ initialize your variables! Don't assume they'll be set to zero by default\n" \
    "* __functions:__ don't call your functions the same as some of your variables\n" \
    "\n" \
    "\n" \
    "### Shadertoy Inputs\n\n" \
    "Type | Name | Function | Description\n" \
    "--- | --- | --- | ---\n" \
    "vec3 | iResolution | image | The viewport resolution (z is pixel aspect ratio, usually 1.0)\n" \
    "float | iTime | image/sound | Current time in seconds\n" \
    "float | iTimeDelta | image | Time it takes to render a frame, in seconds\n" \
    "int | iFrame | image | Current frame\n" \
    "float | iFrameRate | image | Number of frames rendered per second\n" \
    "float | iChannelTime[" STRINGISE (NBINPUTS) "] | image | Time for channel (if video or sound), in seconds\n" \
    "vec3 | iChannelResolution[" STRINGISE (NBINPUTS) "] | image/sound | Input texture resolution for each channel\n" \
    "vec2 | iChannelOffset[" STRINGISE(NBINPUTS) "] | image | Input texture offset in pixel coords for each channel\n" \
    "vec4 | iMouse | image | xy = current pixel coords (if LMB is down). zw = click pixel\n" \
    "sampler2D | iChannel{i} | image/sound | Sampler for input textures i\n" \
    "vec4 | iDate | image/sound | Year, month, day, time in seconds in .xyzw\n" \
    "float | iSampleRate | image/sound | The sound sample rate (typically 44100)\n" \
    "vec2 | iRenderScale | image | The OpenFX render scale (e.g. 0.5,0.5 when rendering half-size) [OFX plugin only]\n" \
    "\n" \
    "### Shadertoy Outputs\n" \
    "For image shaders, fragColor is used as output channel. It is not, for now, mandatory but recommended to leave the alpha channel to 1.0.\n" \
    "\n" \
    "For sound shaders, the mainSound() function returns a vec2 containing the left and right (stereo) sound channel wave data.\n" \
    "\n" \
    "### OpenFX extensions to Shadertoy\n" \
    "\n" \
    "* The pre-defined `iRenderScale` uniform contains the current render scale. Basically all pixel sizes must be multiplied by the renderscale to get a scale-independent effect. For compatibility with Shadertoy, the first line that starts with `const vec2 iRenderScale` is ignored (the full line should be `const vec2 iRenderScale = vec2(1.,1.);`).\n" \
    "* The pre-defined `iChannelOffset` uniform contains the texture offset for each channel relative to channel 0. For compatibility with Shadertoy, the first line that starts with `const vec2 iChannelOffset` is ignored (the full line should be `const vec2 iChannelOffset[4] = vec2[4]( vec2(0.,0.), vec2(0.,0.), vec2(0.,0.), vec2(0.,0.) );`).\n" \
    "* The shader may define additional uniforms, which should have a default value, as in `uniform vec2 blurSize = vec2(5., 5.);`.\n" \
    "  These uniforms can be made available as OpenFX parameters using settings in the 'Extra parameters' group, which can be set automatically using the 'Auto. Params' button (in this case, parameters are updated when the image is rendered).\n" \
    "  A parameter label and help string can be given in the comment on the same line. The help string must be in parenthesis.\n" \
    "  `uniform vec2 blurSize = vec2(5., 5.); // Blur Size (The blur size in pixels.)`\n" \
    "  min/max values can also be given after a comma. The strings must be exactly `min=` and `max=`, without additional spaces, separated by a comma, and the values must have the same dimension as the uniform:\n" \
    "  `uniform vec2 blurSize = vec2(5., 5.); // Blur Size (The blur size in pixels.), min=(0.,0.), max=(1000.,1000.)`\n" \
    "* The following comment line placed in the shader gives a label and help string to input 1 (the comment must be the only thing on the line):\n" \
    "  `// iChannel1: Noise (A noise texture to be used for random number calculations. The texture should not be frame-varying.)`\n" \
    "* This one also sets the filter and wrap parameters:\n" \
    "  `// iChannel0: Source (Source image.), filter=linear, wrap=clamp`\n" \
    "* And this one sets the output bouding box (possible values are Default, Union, Intersection, and iChannel0 to iChannel3):\n" \
    "  `// BBox: iChannel0`\n" \
    "\n" \
    "### Default textures and videos\n" \
    "\n" \
    "The default shadertoy textures and videos are avalaible from the [Shadertoy](http://www.shadertoy.com) web site. In order to mimic the behavior of each shader, download the corresponding textures or videos and connect them to the proper input.\n" \
    "\n" \
    "- Textures: [tex00](https://www.shadertoy.com/presets/tex00.jpg),  [tex01](https://www.shadertoy.com/presets/tex01.jpg),  [tex02](https://www.shadertoy.com/presets/tex02.jpg),  [tex03](https://www.shadertoy.com/presets/tex03.jpg),  [tex04](https://www.shadertoy.com/presets/tex04.jpg),  [tex05](https://www.shadertoy.com/presets/tex05.jpg),  [tex06](https://www.shadertoy.com/presets/tex06.jpg),  [tex07](https://www.shadertoy.com/presets/tex07.jpg),  [tex08](https://www.shadertoy.com/presets/tex08.jpg),  [tex09](https://www.shadertoy.com/presets/tex09.jpg),  [tex10](https://www.shadertoy.com/presets/tex10.png),  [tex11](https://www.shadertoy.com/presets/tex11.png),  [tex12](https://www.shadertoy.com/presets/tex12.png),  [tex14](https://www.shadertoy.com/presets/tex14.png),  [tex15](https://www.shadertoy.com/presets/tex15.png),  [tex16](https://www.shadertoy.com/presets/tex16.png),  [tex17](https://www.shadertoy.com/presets/tex17.jpg),  [tex18](https://www.shadertoy.com/presets/tex18.jpg),  [tex19](https://www.shadertoy.com/presets/tex19.png),  [tex20](https://www.shadertoy.com/presets/tex20.jpg),  [tex21](https://www.shadertoy.com/presets/tex21.png).\n" \
    "- Videos: [vid00](https://www.shadertoy.com/presets/vid00.ogv),  [vid01](https://www.shadertoy.com/presets/vid01.webm),  [vid02](https://www.shadertoy.com/presets/vid02.ogv),  [vid03](https://www.shadertoy.com/presets/vid03.webm).\n" \
    "- Cubemaps: [cube00_0](https://www.shadertoy.com/presets/cube00_0.jpg),  [cube01_0](https://www.shadertoy.com/presets/cube01_0.png),  [cube02_0](https://www.shadertoy.com/presets/cube02_0.jpg),  [cube03_0](https://www.shadertoy.com/presets/cube03_0.png),  [cube04_0](https://www.shadertoy.com/presets/cube04_0.png),  [cube05](https://www.shadertoy.com/presets/cube05_0.png)"

#define kPluginIdentifier "net.sf.openfx.Shadertoy"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false


#define kShaderInputsHint \
    "Shader Inputs:\n" \
    "uniform vec3      iResolution;           // viewport resolution (in pixels)\n" \
    "uniform float     iTime;           // shader playback time (in seconds)\n" \
    "uniform float     iTimeDelta;            // render time (in seconds)\n" \
    "uniform int       iFrame;                // shader playback frame\n" \
    "uniform float     iChannelTime[" STRINGISE (NBINPUTS) "];       // channel playback time (in seconds)\n" \
    "uniform vec3      iChannelResolution[" STRINGISE (NBINPUTS) "]; // channel resolution (in pixels)\n" \
    "uniform vec2      iChannelOffset[" STRINGISE (NBINPUTS) "]; // channel texture offset relative to iChannel0 (in pixels)\n" \
    "uniform vec4      iMouse;                // mouse pixel coords. xy: current (if MLB down), zw: click\n" \
    "uniform samplerXX iChannel0..3;          // input channel. XX = 2D/Cube\n" \
    "uniform vec4      iDate;                 // (year, month, day, time in seconds)\n" \
    "uniform float     iSampleRate;           // sound sample rate (i.e., 44100)\n" \
    ""
#define kParamBBox "bbox"
#define kParamBBoxLabel "Output Bounding Box"
#define kParamBBoxHint "What to use to produce the output image's bounding box. If no selected input is connected, use the project size."
#define kParamBBoxOptionDefault "Default", "Default bounding box (project size).", "default"
#define kParamBBoxOptionFormat "Format", "Use a pre-defined image format.", "format"
//#define kParamBBoxOptionSize "Size", "Use a specific extent (size and offset).", "size"
#define kParamBBoxOptionUnion "Union", "Union of all connected inputs.", "union"
#define kParamBBoxOptionIntersection "Intersect", "Intersection of all connected inputs.", "intersection"
#define kParamBBoxOptionIChannel "iChannel"
#define kParamBBoxOptionIChannelHint "Bounding box of iChannel"
#define kParamBBoxOptionIChannelEnum "iChannel"

#define kParamFormat kNatronParamFormatChoice
#define kParamFormatLabel "Format"
#define kParamFormatHint "The output format."

#define kParamFormatSize kNatronParamFormatSize
#define kParamFormatSizeLabel "Size"
#define kParamFormatSizeHint "The output dimensions of the image in pixels."

#define kParamFormatPAR kNatronParamFormatPar
#define kParamFormatPARLabel "Pixel Aspect Ratio"
#define kParamFormatPARHint "Output pixel aspect ratio."

#define kGroupImageShader "imageShaderGroup"
#define kGroupImageShaderLabel "Image Shader"

#define kParamImageShaderFileName "imageShaderFileName"
#define kParamImageShaderFileNameLabel "Load from File"
#define kParamImageShaderFileNameHint "Load the source from the given file. The file contents is only loaded once. Press the \"Reload\" button to load again the same file."

#define kParamImageShaderReload "imageShaderReload"
#define kParamImageShaderReloadLabel "Reload"
#define kParamImageShaderReloadHint "Reload the source from the given file."

#define kParamImageShaderPresetDir "imageShaderPresetDir"
#define kParamImageShaderPresetDirLabel "Presets Directory"
#define kParamImageShaderPresetDirHint "The directory where presets are located. There must be a \"Shadertoy.txt\" file in this directory to give the list of presets (see the default presets directory for an example). The default textures are located in \"%1\"."

#define kParamImageShaderPreset "imageShaderPreset"
#define kParamImageShaderPresetLabel "Load from Preset"
#define kParamImageShaderPresetHint "Load the source from the preset. The default textures are located in \"%1\", and more presets can be added by editing \"Shadertoy.txt\" in the Presets Directory."

#define kParamImageShaderSource "imageShaderSource"
#define kParamImageShaderSourceLabel "Source"
#define kParamImageShaderSourceHint "Image shader.\n\n" kShaderInputsHint

#define kParamImageShaderCompile "imageShaderCompile"
#define kParamImageShaderCompileLabel "Compile"
#define kParamImageShaderCompileHint "Compile the image shader."

// parameter to trigger a new render and make sure the shader is compiled
#define kParamImageShaderTriggerRender "imageShaderTriggerRender"

// parameter used to trigger an InstanceChanged once the Shader was compiled in the render function and parameters were updated
#define kParamImageShaderParamsUpdated "imageShaderParamsUpdated"

#define kParamAuto "autoParams"
#define kParamAutoLabel "Auto. Params"
#define kParamAutoHint "Automatically set the parameters from the shader source next time image is rendered. May require clicking twice, depending on the OpenFX host. Also reset these parameters to their default value."

#define kParamResetParams "resetParams"
#define kParamResetParamsLabel "Reset Params Values"
#define kParamResetParamsHint "Set all the extra parameters to their default values, as set automatically by the \"Auto. Params\", or in the \"Extra Parameters\" group."


#define kParamImageShaderDefault                            \
    "// iChannel0: Source (Source image.), filter=linear, wrap=clamp\n" \
    "// BBox: iChannel0\n" \
    "\n" \
    "const vec2 iRenderScale = vec2(1.,1.); // Render Scale (The size of a full-resolution pixel).\n" \
    "uniform float amplitude = 0.5; // Amplitude (The amplitude of the xy sine wave), min=0., max=1.\n" \
    "uniform float size = 50.; // Size (The period of the xy sine wave), min = 0., max = 200.\n" \
    "\n" \
    "void mainImage( out vec4 fragColor, in vec2 fragCoord )\n" \
    "{\n"                                                       \
    "    vec2 uv = fragCoord.xy / iResolution.xy;\n"            \
    "    vec3 sinetex = vec3(0.5+0.5*amplitude*sin(fragCoord.x/(size*iRenderScale.x)),\n" \
    "                        0.5+0.5*amplitude*sin(fragCoord.y/(size*iRenderScale.y)),\n" \
    "                        0.5+0.5*sin(iTime));\n" \
    "    fragColor = vec4(amplitude*sinetex + (1 - amplitude)*texture2D( iChannel0, uv ).xyz,1.0);\n"  \
    "}"

// mouse parameters, see:
// https://www.shadertoy.com/view/Mss3zH
// https://www.shadertoy.com/view/4sf3RN
// https://www.shadertoy.com/view/XsGSDz
#define kParamMouseParams "mouseParams"
#define kParamMouseParamsLabel "Mouse Params."
#define kParamMouseParamsHint "Enable mouse parameters."

#define kParamMousePosition "mousePosition"
#define kParamMousePositionLabel "Mouse Pos."
#define kParamMousePositionHint "Mouse position, in pixels. Gets mapped to the xy components of the iMouse input. Note that in the web version of Shadertoy, the y coordinate goes from 1 to height."

#define kParamMouseClick "mouseClick"
#define kParamMouseClickLabel "Click Pos."
#define kParamMouseClickHint "Mouse click position, in pixels. The zw components of the iMouse input contain mouseClick if mousePressed is checked, else -mouseClick. The default is (1.,1.)"

#define kParamMousePressed "mousePressed"
#define kParamMousePressedLabel "Mouse Pressed"
#define kParamMousePressedHint "When checked, the zw components of the iMouse input contain mouseClick, else they contain -mouseClick. If the host does not support animating this parameter, use negative values for mouseClick to emulate a released mouse button."

#define kParamDate "startDate"
#define kParamDateLabel "Start Date"
#define kParamDateHint "The date (yyyy,mm,dd,s) corresponding to frame 0. The month starts at 0 for january, the day starts at 1, and the seconds start from 0 at midnight and should be at most 24*60*60=86400. December 28, 1895 at 10:30 would thus the be (1895,11,28,37800)."

#define kGroupExtraParameters "extraParametersGroup"
#define kGroupExtraParametersLabel "Extra Parameters"
#define kGroupExtraParametersHint "Description of extra parameters (a.k.a. uniforms) used by the shader source. Note that these parameters must be explicitely declared as uniforms in the shader (to keep compatibility with shadertoy, they may also have a default value set in the shader source)."

#define kGroupParameter "extraParameterGroup"
#define kGroupParameterLabel "Param "

#define kParamCount "paramCount"
#define kParamCountLabel "No. of Params"
#define kParamCountHint "Number of extra parameters."

#define kParamType "paramType" // followed by param number
#define kParamTypeLabel "Type"
#define kParamTypeHint "Type of the parameter."
#define kParamTypeOptionNone "none", "No parameter.", "none"
#define kParamTypeOptionBool "bool", "Boolean parameter (checkbox).", "bool"
#define kParamTypeOptionInt "int", "Integer parameter.", "int"
#define kParamTypeOptionFloat "float", "Floating-point parameter.", "float"
#define kParamTypeOptionVec2 "vec2", "2D floating-point parameter (e.g. position).", "vec2"
#define kParamTypeOptionVec3 "vec3", "3D floating-point parameter (e.g. 3D position or RGB color).", "vec3"
#define kParamTypeOptionVec4 "vec4", "4D floating-point parameter (e.g. RGBA color).", "vec4"

#define kParamName "paramName" // followed by param number
#define kParamNameLabel "Name"
#define kParamNameHint "Name of the parameter, as used in the shader."

#define kParamLabel "paramLabel" // followed by param number
#define kParamLabelLabel "Label"
#define kParamLabelHint "Label of the parameter, as displayed in the user interface."

#define kParamHint "paramHint" // followed by param number
#define kParamHintLabel "Hint"
#define kParamHintHint "Help for the parameter."

#define kParamValue "paramValue"
#define kParamValueBool kParamValue "Bool" // followed by param number
#define kParamValueInt kParamValue "Int" // followed by param number
#define kParamValueFloat kParamValue "Float" // followed by param number
#define kParamValueVec2 kParamValue "Vec2" // followed by param number
#define kParamValueVec3 kParamValue "Vec3" // followed by param number
#define kParamValueVec4 kParamValue "Vec4" // followed by param number
#define kParamValueLabel "Value" // followed by param number
#define kParamValueHint "Value of the parameter."

#define kParamDefault "paramDefault"
#define kParamDefaultBool kParamDefault "Bool" // followed by param number
#define kParamDefaultInt kParamDefault "Int" // followed by param number
#define kParamDefaultFloat kParamDefault "Float" // followed by param number
#define kParamDefaultVec2 kParamDefault "Vec2" // followed by param number
#define kParamDefaultVec3 kParamDefault "Vec3" // followed by param number
#define kParamDefaultVec4 kParamDefault "Vec4" // followed by param number
#define kParamDefaultLabel "Default" // followed by param number
#define kParamDefaultHint "Default value of the parameter."

#define kParamMin "paramMin"
#define kParamMinInt kParamMin "Int" // followed by param number
#define kParamMinFloat kParamMin "Float" // followed by param number
#define kParamMinVec2 kParamMin "Vec2" // followed by param number
#define kParamMinLabel "Min" // followed by param number
#define kParamMinHint "Min value of the parameter."

#define kParamMax "paramMax"
#define kParamMaxInt kParamMax "Int" // followed by param number
#define kParamMaxFloat kParamMax "Float" // followed by param number
#define kParamMaxVec2 kParamMax "Vec2" // followed by param number
#define kParamMaxLabel "Max" // followed by param number
#define kParamMaxHint "Max value of the parameter."

#define kParamInputFilter "mipmap"
#define kParamInputFilterLabel "Filter"
#define kParamInputFilterHint "Texture filter for this input."
#define kParamInputFilterOptionNearest "Nearest", "MIN/MAG = GL_NEAREST/GL_NEAREST", "nearest"
#define kParamInputFilterOptionLinear "Linear", "MIN/MAG = GL_LINEAR/GL_LINEAR", "linear"
#define kParamInputFilterOptionMipmap "Mipmap", "MIN/MAG = GL_LINEAR_MIPMAP_LINEAR/GL_LINEAR", "mipmap"
#define kParamInputFilterOptionAnisotropic "Anisotropic", "Mipmap with anisotropic filtering. Available with GPU if supported (check for the presence of the GL_EXT_texture_filter_anisotropic extension in the Renderer Info) and with \"softpipe\" CPU driver.", "anisotropic"

#define kParamInputWrap "wrap"
#define kParamInputWrapLabel "Wrap", "Texture wrap parameter for this input."
#define kParamInputWrapOptionRepeat "Repeat", "WRAP_S/T = GL_REPEAT", "repeat"
#define kParamInputWrapOptionClamp "Clamp", "WRAP_S/T = GL_CLAMP_TO_EDGE", "clamp"
#define kParamInputWrapOptionMirror "Mirror", "WRAP_S/T = GL_MIRRORED_REPEAT", "mirror"

#define kParamInputName "inputName" // name for the label for each input

#define kParamInputEnable "inputEnable"
#define kParamInputEnableLabel "Enable"
#define kParamInputEnableHint "Enable this input."

#define kParamInputLabel "inputLabel"
#define kParamInputLabelLabel "Label"
#define kParamInputLabelHint "Label for this input in the user interface."

#define kParamInputHint "inputHint"
#define kParamInputHintLabel "Hint"
#define kParamInputHintHint "Help for this input."

#if defined(OFX_SUPPORTS_OPENGLRENDER) && defined(HAVE_OSMESA)
#define kParamEnableGPU "enableGPU"
#define kParamEnableGPULabel "Enable GPU Render"
#define kParamEnableGPUHint \
    "Enable GPU-based OpenGL render.\n" \
    "If the checkbox is checked but is not enabled (i.e. it cannot be unchecked), GPU render can not be enabled or disabled from the plugin and is probably part of the host options.\n" \
    "If the checkbox is not checked and is not enabled (i.e. it cannot be checked), GPU render is not available on this host."
#endif

#ifdef HAVE_OSMESA
#define kParamCPUDriver "cpuDriver"
#define kParamCPUDriverLabel "CPU Driver"
#define kParamCPUDriverHint "Driver for CPU rendering. May be \"softpipe\" , \"llvmpipe\"  or \"swr\" (OpenSWR, not always available)."
#define kParamCPUDriverOptionSoftPipe "softpipe", "Gallium softpipe driver from Mesa. A reference signle-threaded driver (slower, has GL_EXT_texture_filter_anisotropic GL_ARB_texture_query_lod GL_ARB_pipeline_statistics_query).", "softpipe"
#define kParamCPUDriverOptionLLVMPipe "llvmpipe", "Gallium llvmpipe driver from Mesa, if available. Uses LLVM for x86 JIT code generation and is multi-threaded (faster, has GL_ARB_buffer_storage GL_EXT_polygon_offset_clamp).", "llvmpipe"
#define kParamCPUDriverOptionSWR "swr", "OpenSWR driver from Mesa, if available. Fully utilizes modern instruction sets like AVX and AVX2 to achieve high rendering performance.", "swr"
#define kParamCPUDriverDefault ShadertoyPlugin::eCPUDriverLLVMPipe
#endif

#define kParamRendererInfo "rendererInfo"
#define kParamRendererInfoLabel "Renderer Info..."
#define kParamRendererInfoHint "Retrieve information about the current OpenGL renderer."

#define kParamHelp "helpButton"
#define kParamHelpLabel "Help..."
#define kParamHelpHint "Display help about using Shadertoy."

#define kClipChannel "iChannel"


static std::vector<ShadertoyPlugin::Preset> gPresetsDefault;

static
bool
replace(string& str, const string& from, const string& to)
{
    size_t start_pos = str.find(from);
    if(start_pos == string::npos)
        return false;
    str.replace(start_pos, from.length(), to);
    return true;
}

static
string
unsignedToString(unsigned i)
{
    if (i == 0) {
        return "0";
    }
    string nb;
    for (unsigned j = i; j != 0; j /= 10) {
        nb = (char)( '0' + (j % 10) ) + nb;
    }

    return nb;
}

static
void
presetsFromDir(const string &dir, std::vector<ShadertoyPlugin::Preset>& presets)
{
    //std::printf( kOfxPluginPropFilePath"= %s", filePath.c_str() );
    char line[1024];
    presets.clear();
    FILE* fp = std::fopen( (dir + "/Shadertoy.txt").c_str(), "r" );
    if (fp != NULL) {
        //int i = 0;
        while (1) {
            if (std::fgets(line, sizeof(line), fp) == NULL) {
                break;
            }
            //++i;
            //printf("%3d: %s", i, line);
            if (line[0] == '#') { // skip comments
                continue;
            }
            // a line looks like
            //    {"Ball",                            "ball.frag.glsl",                 99,-1,-1,-1},
            const char* desc = std::strchr(line, '"');
            if (desc == NULL) {
                continue;
            }
            ++desc;
            const char* desc_end = std::strchr(desc, '"');
            if (desc_end == NULL) {
                continue;
            }
            string description(desc, desc_end);
            ++desc_end;
            const char* file = std::strchr(desc_end, '"');
            if (file == NULL) {
                continue;
            }
            ++file;
            const char* file_end = std::strchr(file, '"');
            if (file_end == NULL) {
                continue;
            }
            string filename = dir + '/' + string(file, file_end);
            //printf("%s,%s\n", description.c_str(), filename.c_str());
            // check if file is readable
            FILE* fps = std::fopen( filename.c_str(), "r" );
            if (fps == NULL) {
                //printf("%s cannot open\n", filename.c_str());
                continue;
            }
            std::fclose(fps);
            presets.push_back( ShadertoyPlugin::Preset(description, filename) );
        }
        std::fclose(fp);
    }
}

double
ShadertoyPlugin::ftod(float f)
{
    double      d, exponent, mantissa, power10;
    bool        bNegative;

#ifdef FTOD_10_TO_6TH
#undef FTOD_10_TO_6TH
#endif
#ifdef FTOD_10_TO_7TH
#undef FTOD_10_TO_7TH
#endif
#define FTOD_10_TO_6TH  ((double) 1000000.0)
#define FTOD_10_TO_7TH  ((double)10000000.0)

    d = (double)f;
    if (d == (double)0.0) {
        return d;
    }
    if (d < (double)0.0) {
        d = -d;
        bNegative = true;
    } else {
        bNegative = false;
    }

    exponent = std::floor(std::log10(d) + (double)0.00005);
    power10 = std::pow( (double)10.0, exponent );
    mantissa = d / power10;
    if (mantissa < (double)1.0) {
        d = (floor((mantissa*FTOD_10_TO_7TH)+0.5)/FTOD_10_TO_7TH) * power10;
    } else {
        d = (floor((mantissa*FTOD_10_TO_6TH)+ 0.5)/FTOD_10_TO_6TH) * power10;
    }
    return (bNegative ? -d : d);
}

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */

ShadertoyPlugin::ShadertoyPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(NULL)
    , _srcClips(NBINPUTS, (Clip*)        NULL)
    , _inputEnable  (NBINPUTS, (BooleanParam*) NULL)
    , _inputLabel   (NBINPUTS, (StringParam*) NULL)
    , _inputHint    (NBINPUTS, (StringParam*) NULL)
    , _inputFilter  (NBINPUTS, (ChoiceParam*) NULL)
    , _inputWrap    (NBINPUTS, (ChoiceParam*) NULL)
    , _bbox(NULL)
    , _format(NULL)
    , _formatSize(NULL)
    , _formatPar(NULL)
    , _imageShaderFileName(NULL)
    , _imageShaderPresetDir(NULL)
    , _imageShaderPreset(NULL)
    , _imageShaderSource(NULL)
    , _imageShaderCompile(NULL)
    , _imageShaderTriggerRender(NULL)
    , _imageShaderParamsUpdated(NULL)
    , _mouseParams(NULL)
    , _mousePosition(NULL)
    , _mouseClick(NULL)
    , _mousePressed(NULL)
    , _date(NULL)
    , _groupExtra(NULL)
    , _paramCount(NULL)
    , _paramGroup     (NBUNIFORMS, (GroupParam*)   NULL)
    , _paramType      (NBUNIFORMS, (ChoiceParam*)  NULL)
    , _paramName      (NBUNIFORMS, (StringParam*)  NULL)
    , _paramLabel     (NBUNIFORMS, (StringParam*)  NULL)
    , _paramHint      (NBUNIFORMS, (StringParam*)  NULL)
    , _paramValueBool (NBUNIFORMS, (BooleanParam*) NULL)
    , _paramValueInt  (NBUNIFORMS, (IntParam*)     NULL)
    , _paramValueFloat(NBUNIFORMS, (DoubleParam*)  NULL)
    , _paramValueVec2 (NBUNIFORMS, (Double2DParam*)     NULL)
    , _paramValueVec3 (NBUNIFORMS, (RGBParam*)NULL)
    , _paramValueVec4 (NBUNIFORMS, (RGBAParam*)    NULL)
    , _paramDefaultBool (NBUNIFORMS, (BooleanParam*) NULL)
    , _paramDefaultInt  (NBUNIFORMS, (IntParam*)     NULL)
    , _paramDefaultFloat(NBUNIFORMS, (DoubleParam*)  NULL)
    , _paramDefaultVec2 (NBUNIFORMS, (Double2DParam*)     NULL)
    , _paramDefaultVec3 (NBUNIFORMS, (RGBParam*)NULL)
    , _paramDefaultVec4 (NBUNIFORMS, (RGBAParam*)    NULL)
    , _paramMinInt  (NBUNIFORMS, (IntParam*)     NULL)
    , _paramMinFloat(NBUNIFORMS, (DoubleParam*)  NULL)
    , _paramMinVec2 (NBUNIFORMS, (Double2DParam*)     NULL)
    , _paramMaxInt  (NBUNIFORMS, (IntParam*)     NULL)
    , _paramMaxFloat(NBUNIFORMS, (DoubleParam*)  NULL)
    , _paramMaxVec2 (NBUNIFORMS, (Double2DParam*)     NULL)
    , _enableGPU(NULL)
    , _cpuDriver(NULL)
    , _imageShaderID(1)
    , _imageShaderUniformsID(1)
    , _imageShaderUpdateParams(false)
    , _imageShaderExtraParameters()
    , _imageShaderHasMouse(false)
    , _imageShaderInputEnabled(NBINPUTS)
    , _imageShaderInputLabel(NBINPUTS)
    , _imageShaderInputHint(NBINPUTS)
    , _imageShaderInputFilter(NBINPUTS, eFilterMipmap)
    , _imageShaderInputWrap(NBINPUTS, eWrapRepeat)
    , _imageShaderBBox(eBBoxDefault)
    , _imageShaderCompiled(false)
    , _openGLContextData()
    , _openGLContextAttached(false)
    , _presets(gPresetsDefault)
{
    try {
        _imageShaderMutex.reset(new Mutex);
        _rendererInfoMutex.reset(new Mutex);
#if defined(HAVE_OSMESA)
        _osmesaMutex.reset(new Mutex);
#endif
    } catch (const std::exception& e) {
#      ifdef DEBUG
        std::cout << "ERROR in createInstance(): Multithread::Mutex creation returned " << e.what() << std::endl;
#      endif
    }

    _dstClip = fetchClip(kOfxImageEffectOutputClipName);
    assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGBA ||
                         _dstClip->getPixelComponents() == ePixelComponentAlpha) );
    switch ( getContext() ) {
    case eContextFilter:
        _srcClips[0] = fetchClip(kOfxImageEffectSimpleSourceClipName);
        for (unsigned j = 1; j < NBINPUTS; ++j) {
            _srcClips[j] = fetchClip( string(kClipChannel) + unsignedToString(j) );
        }
        break;
    case eContextGenerator:
    case eContextGeneral:
    default:
        for (unsigned j = 0; j < NBINPUTS; ++j) {
            _srcClips[j] = fetchClip( string(kClipChannel) + unsignedToString(j) );
        }
        break;
    }
    for (unsigned i = 0; i < NBINPUTS; ++i) {
        assert( (!_srcClips[i] && getContext() == eContextGenerator) ||
                ( _srcClips[i] && (_srcClips[i]->getPixelComponents() == ePixelComponentRGBA ||
                                   _srcClips[i]->getPixelComponents() == ePixelComponentAlpha) ) );
        string nb = unsignedToString(i);
        _inputEnable[i] = fetchBooleanParam(kParamInputEnable + nb);
        _inputLabel[i] = fetchStringParam(kParamInputLabel + nb);
        _inputHint[i] = fetchStringParam(kParamInputHint + nb);
        _inputFilter[i] = fetchChoiceParam(kParamInputFilter + nb);
        _inputWrap[i] = fetchChoiceParam(kParamInputWrap + nb);
        assert(_inputEnable[i] && _inputLabel[i] && _inputHint[i] && _inputFilter[i] && _inputWrap[i]);
    }

    _bbox = fetchChoiceParam(kParamBBox);
    _format = fetchChoiceParam(kParamFormat);
    _formatSize = fetchInt2DParam(kParamFormatSize);
    _formatPar = fetchDoubleParam(kParamFormatPAR);
    assert(_bbox && _format && _formatSize && _formatPar);
    _imageShaderFileName = fetchStringParam(kParamImageShaderFileName);
    if ( paramExists(kParamImageShaderPresetDir) ) {
        _imageShaderPresetDir = fetchStringParam(kParamImageShaderPresetDir);
        _imageShaderPreset = fetchChoiceParam(kParamImageShaderPreset);
    }
    _imageShaderSource = fetchStringParam(kParamImageShaderSource);
    _imageShaderCompile = fetchPushButtonParam(kParamImageShaderCompile);
    _imageShaderTriggerRender = fetchIntParam(kParamImageShaderTriggerRender);
    _imageShaderParamsUpdated = fetchBooleanParam(kParamImageShaderParamsUpdated);
    assert(_imageShaderFileName && _imageShaderSource && _imageShaderCompile && _imageShaderTriggerRender && _imageShaderParamsUpdated);
    _mouseParams = fetchBooleanParam(kParamMouseParams);
    assert(_mouseParams);
    _mousePosition = fetchDouble2DParam(kParamMousePosition);
    _mouseClick = fetchDouble2DParam(kParamMouseClick);
    _mousePressed = fetchBooleanParam(kParamMousePressed);
    assert(_mousePosition && _mousePressed && _mouseClick);
    _date = fetchRGBAParam(kParamDate);
    assert(_date);
    _groupExtra = fetchGroupParam(kGroupExtraParameters);
    _paramCount = fetchIntParam(kParamCount);
    assert(_groupExtra && _paramCount);
    const ImageEffectHostDescription &gHostDescription = *getImageEffectHostDescription();
    const unsigned int nbuniforms = (gHostDescription.hostName == "uk.co.thefoundry.nuke" && gHostDescription.versionMajor == 7) ? SHADERTOY_NBUNIFORMS_NUKE7 : NBUNIFORMS; //if more than 7, Nuke 7's parameter page goes blank when unfolding the Extra Parameters group
    _paramGroup.resize(nbuniforms);
    _paramType.resize(nbuniforms);
    _paramName.resize(nbuniforms);
    _paramLabel.resize(nbuniforms);
    _paramHint.resize(nbuniforms);
    _paramValueBool.resize(nbuniforms);
    _paramValueInt.resize(nbuniforms);
    _paramValueFloat.resize(nbuniforms);
    _paramValueVec2.resize(nbuniforms);
    _paramValueVec3.resize(nbuniforms);
    _paramValueVec4.resize(nbuniforms);
    _paramDefaultBool.resize(nbuniforms);
    _paramDefaultInt.resize(nbuniforms);
    _paramDefaultFloat.resize(nbuniforms);
    _paramDefaultVec2.resize(nbuniforms);
    _paramDefaultVec3.resize(nbuniforms);
    _paramDefaultVec4.resize(nbuniforms);
    _paramMinInt.resize(nbuniforms);
    _paramMinFloat.resize(nbuniforms);
    _paramMinVec2.resize(nbuniforms);
    _paramMaxInt.resize(nbuniforms);
    _paramMaxFloat.resize(nbuniforms);
    _paramMaxVec2.resize(nbuniforms);
    for (unsigned i = 0; i < nbuniforms; ++i) {
        // generate the number string
        string nb = unsignedToString(i);
        _paramGroup[i]      = fetchGroupParam   (kGroupParameter  + nb);
        _paramType[i]       = fetchChoiceParam  (kParamType       + nb);
        _paramName[i]       = fetchStringParam  (kParamName       + nb);
        _paramLabel[i]      = fetchStringParam  (kParamLabel      + nb);
        _paramHint[i]       = fetchStringParam  (kParamHint       + nb);
        _paramValueBool[i]  = fetchBooleanParam (kParamValueBool  + nb);
        _paramValueInt[i]   = fetchIntParam     (kParamValueInt   + nb);
        _paramValueFloat[i] = fetchDoubleParam  (kParamValueFloat + nb);
        _paramValueVec2[i]  = fetchDouble2DParam(kParamValueVec2  + nb);
        _paramValueVec3[i]  = fetchRGBParam     (kParamValueVec3  + nb);
        _paramValueVec4[i]  = fetchRGBAParam    (kParamValueVec4  + nb);
        _paramDefaultBool[i]  = fetchBooleanParam (kParamDefaultBool  + nb);
        _paramDefaultInt[i]   = fetchIntParam     (kParamDefaultInt   + nb);
        _paramDefaultFloat[i] = fetchDoubleParam  (kParamDefaultFloat + nb);
        _paramDefaultVec2[i]  = fetchDouble2DParam(kParamDefaultVec2  + nb);
        _paramDefaultVec3[i]  = fetchRGBParam(kParamDefaultVec3  + nb);
        _paramDefaultVec4[i]  = fetchRGBAParam    (kParamDefaultVec4  + nb);
        _paramMinInt[i]   = fetchIntParam     (kParamMinInt   + nb);
        _paramMinFloat[i] = fetchDoubleParam  (kParamMinFloat + nb);
        _paramMinVec2[i]  = fetchDouble2DParam(kParamMinVec2  + nb);
        _paramMaxInt[i]   = fetchIntParam     (kParamMaxInt   + nb);
        _paramMaxFloat[i] = fetchDoubleParam  (kParamMaxFloat + nb);
        _paramMaxVec2[i]  = fetchDouble2DParam(kParamMaxVec2  + nb);
        assert(_paramGroup[i] && _paramType[i] && _paramName[i] && _paramLabel[i] && _paramHint[i] && _paramValueBool[i] && _paramValueInt[i] && _paramValueFloat[i] && _paramValueVec2[i] && _paramValueVec3[i] && _paramValueVec4[i]);
    }
#if defined(OFX_SUPPORTS_OPENGLRENDER) && defined(HAVE_OSMESA)
    _enableGPU = fetchBooleanParam(kParamEnableGPU);
    assert(_enableGPU);
    if (!gHostDescription.supportsOpenGLRender) {
        _enableGPU->setEnabled(false);
    }
    setSupportsOpenGLRender( _enableGPU->getValue() );
#endif
    updateExtra();
    updateVisibility();
    updateClips();
    initOpenGL();
#if defined(HAVE_OSMESA)
    if ( OSMesaDriverSelectable() ) {
        _cpuDriver = fetchChoiceParam(kParamCPUDriver);
    }
    initMesa();
#endif
    _imageShaderCompile->setEnabled(false); // always compile on first render

    // Trigger a render, so that the shader is compiled and parameters are updated.
    // OpenFX allows this, see http://openfx.sourceforge.net/Documentation/1.4/ofxProgrammingReference.html#SettingParams
    // ... but also forbids this, see http://openfx.sourceforge.net/Documentation/1.4/ofxProgrammingReference.html#OfxParameterSuiteV1_paramSetValue
    // TODO: only do if necessary
    _imageShaderTriggerRender->setValue(_imageShaderTriggerRender->getValue() + 1);
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
ShadertoyPlugin::render(const RenderArguments &args)
{
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    for (unsigned i = 0; i < NBINPUTS; ++i) {
        assert( kSupportsMultipleClipPARs   || !_srcClips[i] || _srcClips[i]->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
        assert( kSupportsMultipleClipDepths || !_srcClips[i] || _srcClips[i]->getPixelDepth()       == _dstClip->getPixelDepth() );
    }

    bool openGLRender = false;
#if defined(OFX_SUPPORTS_OPENGLRENDER)
    openGLRender = args.openGLEnabled;

    if (getImageEffectHostDescription()->hostName.compare(0, 14, "DaVinciResolve") == 0) {
        // DaVinci Resolve advertises GL supported but doesn't enable it here :|
        openGLRender = true;
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
    throwSuiteStatusException(kOfxStatFailed);
}

// overriding getRegionOfDefinition is necessary to tell the host that we do not support render scale
bool
ShadertoyPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                       OfxRectD &rod)
{
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);
    }

    const double time = args.time;
    int bboxChoice = _bbox->getValueAtTime(time);
    if (bboxChoice == eBBoxDefault) {
        // use the default RoD
        return false;
    }
    if (bboxChoice == eBBoxFormat) {
        int w, h;
        _formatSize->getValueAtTime(time, w, h);
        double par = _formatPar->getValueAtTime(time);
        OfxRectI pixelFormat;
        pixelFormat.x1 = pixelFormat.y1 = 0;
        pixelFormat.x2 = w;
        pixelFormat.y2 = h;
        OfxPointD renderScale = {1., 1.};
        Coords::toCanonical(pixelFormat, renderScale, par, &rod);

        return true;
    }
    /*if (bboxChoice == eBBoxSize) {
        _size->getValueAtTime(time, rod.x2, rod.y2);
        _btmLeft->getValueAtTime(time, rod.x1, rod.y1);
        rod.x2 += rod.x1;
        rod.y2 += rod.y1;

        return true;
       }*/

    bool inputEnable[NBINPUTS];
    for (unsigned i = 0; i < NBINPUTS; ++i) {
        inputEnable[i] = _inputEnable[i]->getValue();
    }

    if (bboxChoice >= eBBoxIChannel) {
        unsigned i = bboxChoice - eBBoxIChannel;
        if ( inputEnable[i] && _srcClips[i] && _srcClips[i]->isConnected() ) {
            rod = _srcClips[i]->getRegionOfDefinition(time);

            return true;
        }

        // use the default RoD
        return false;
    }

    std::vector<OfxRectD> rods;
    for (unsigned i = 0; i < NBINPUTS; ++i) {
        if ( inputEnable[i] && _srcClips[i] && _srcClips[i]->isConnected() ) {
            rods.push_back( _srcClips[i]->getRegionOfDefinition(time) );
        }
    }
    if (rods.size() == 0) {
        return false;
    }
    rod = rods[0];
    if (bboxChoice == eBBoxUnion) { //union
        for (unsigned i = 1; i < rods.size(); ++i) {
            Coords::rectBoundingBox(rod, rods[i], &rod);
        }
    } else {  //intersection
        for (unsigned i = 1; i < rods.size(); ++i) {
            Coords::rectIntersection(rod, rods[i], &rod);
        }
        // may return an empty RoD if intersection is empty
    }

    return true;
} // ShadertoyPlugin::getRegionOfDefinition

void
ShadertoyPlugin::getRegionsOfInterest(const RegionsOfInterestArguments &args,
                                      RegionOfInterestSetter &rois)
{
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    bool inputEnable[NBINPUTS];
    for (unsigned i = 0; i < NBINPUTS; ++i) {
        inputEnable[i] = _inputEnable[i]->getValue();
    }

    // The effect requires full images to render any region
    for (unsigned i = 0; i < NBINPUTS; ++i) {
        OfxRectD srcRoI;

        if ( inputEnable[i] && _srcClips[i] && _srcClips[i]->isConnected() ) {
            srcRoI = _srcClips[i]->getRegionOfDefinition(args.time);
            rois.setRegionOfInterest(*_srcClips[i], srcRoI);
        }
    }
}

void
ShadertoyPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    // We have to do this because the processing code does not support varying components for srcClip and dstClip
    // (The OFX spec doesn't state a default value for this)
    if (_srcClips[0]) {
        clipPreferences.setClipComponents( *_dstClip, _srcClips[0]->getUnmappedPixelComponents() );
    }
    clipPreferences.setOutputFrameVarying(true);
    clipPreferences.setOutputHasContinuousSamples(true);
    BBoxEnum bbox = (BBoxEnum)_bbox->getValue();
    if (bbox == eBBoxFormat) {
        int w, h;
        _formatSize->getValue(w, h);
        double par = _formatPar->getValue();
        OfxRectI pixelFormat;
        pixelFormat.x1 = pixelFormat.y1 = 0;
        pixelFormat.x2 = w;
        pixelFormat.y2 = h;
        clipPreferences.setOutputFormat(pixelFormat);
        clipPreferences.setPixelAspectRatio(*_dstClip, par);
    }
}

static inline
bool
starts_with(const string &str,
            const string &prefix)
{
    return (str.substr( 0, prefix.size() ) == prefix);
}

void
ShadertoyPlugin::updateVisibility()
{
    BBoxEnum bbox = (BBoxEnum)_bbox->getValue();
    bool hasFormat = (bbox == eBBoxFormat);

    //bool hasSize = (bbox == eBBoxSize);

    _format->setIsSecretAndDisabled(!hasFormat);
    //_size->setIsSecretAndDisabled(!hasSize);
    //_recenter->setIsSecretAndDisabled(!hasSize);
    //_btmLeft->setIsSecretAndDisabled(!hasSize);

    bool mouseParams = _mouseParams->getValue();
    _mousePosition->setIsSecretAndDisabled(!mouseParams);
    _mouseClick->setIsSecretAndDisabled(!mouseParams);
    _mousePressed->setIsSecretAndDisabled(!mouseParams);

    unsigned paramCount = std::max( 0, std::min(_paramCount->getValue(), (int)_paramType.size()) );
    for (unsigned i = 0; i < _paramType.size(); ++i) {
        updateVisibilityParam(i, i < paramCount);
    }
    for (unsigned i = 0; i < NBINPUTS; ++i) {
        bool enabled = _inputEnable[i]->getValue();
        //_srcClips[i]->setIsSecretAndDisabled(!enabled);
        _inputLabel[i]->setIsSecretAndDisabled(!enabled);
        _inputHint[i]->setIsSecretAndDisabled(!enabled);
        _inputFilter[i]->setIsSecretAndDisabled(!enabled);
        _inputWrap[i]->setIsSecretAndDisabled(!enabled);
    }
}

void
ShadertoyPlugin::updateClips()
{
    for (unsigned i = 0; i < NBINPUTS; ++i) {
        bool enabled = _inputEnable[i]->getValue();
        _srcClips[i]->setIsSecret(!enabled);
        string s;
        _inputLabel[i]->getValue(s);
        if ( s.empty() ) {
            string iChannelX(kClipChannel);
            iChannelX += unsignedToString(i);
            _srcClips[i]->setLabel(iChannelX);
        } else {
            _srcClips[i]->setLabel(s);
        }
        _inputHint[i]->getValue(s);
        _srcClips[i]->setHint(s);
    }
}

void
ShadertoyPlugin::updateVisibilityParam(unsigned i,
                                       bool visible)
{
    UniformTypeEnum paramType = (UniformTypeEnum)_paramType[i]->getValue();
    bool isBool = false;
    bool isInt = false;
    bool isFloat = false;
    bool isVec2 = false;
    bool isVec3 = false;
    bool isVec4 = false;

    string name;
    _paramName[i]->getValue(name);
    if ( visible && !name.empty() ) {
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

    // close the group if it becomes invisible
    if (!visible) {
        _paramGroup[i]->setOpen(false);
    }
    _paramGroup[i]->setIsSecretAndDisabled(!visible);
    _paramType[i]->setIsSecretAndDisabled(!visible);
    _paramName[i]->setIsSecretAndDisabled(!visible);
    _paramLabel[i]->setIsSecretAndDisabled( !visible || name.empty() );
    _paramHint[i]->setIsSecretAndDisabled( !visible || name.empty() );
    _paramValueBool[i]->setIsSecretAndDisabled(!isBool);
    _paramValueInt[i]->setIsSecretAndDisabled(!isInt);
    _paramValueFloat[i]->setIsSecretAndDisabled(!isFloat);
    _paramValueVec2[i]->setIsSecretAndDisabled(!isVec2);
    _paramValueVec3[i]->setIsSecretAndDisabled(!isVec3);
    _paramValueVec4[i]->setIsSecretAndDisabled(!isVec4);
    _paramDefaultBool[i]->setIsSecretAndDisabled(!isBool);
    _paramDefaultInt[i]->setIsSecretAndDisabled(!isInt);
    _paramDefaultFloat[i]->setIsSecretAndDisabled(!isFloat);
    _paramDefaultVec2[i]->setIsSecretAndDisabled(!isVec2);
    _paramDefaultVec3[i]->setIsSecretAndDisabled(!isVec3);
    _paramDefaultVec4[i]->setIsSecretAndDisabled(!isVec4);
    _paramMinInt[i]->setIsSecretAndDisabled(!isInt);
    _paramMinFloat[i]->setIsSecretAndDisabled(!isFloat);
    _paramMinVec2[i]->setIsSecretAndDisabled(!isVec2);
    //_paramMinVec3[i]->setIsSecretAndDisabled(!isVec3);
    //_paramMinVec4[i]->setIsSecretAndDisabled(!isVec4);
    _paramMaxInt[i]->setIsSecretAndDisabled(!isInt);
    _paramMaxFloat[i]->setIsSecretAndDisabled(!isFloat);
    _paramMaxVec2[i]->setIsSecretAndDisabled(!isVec2);
    //_paramMaxVec3[i]->setIsSecretAndDisabled(!isVec3);
    //_paramMaxVec4[i]->setIsSecretAndDisabled(!isVec4);
} // ShadertoyPlugin::updateVisibilityParam

// for each extra parameter that has a nonempty name and a type, set the label of its Value param to its name.
// If the label is not the default label, then it was set by the host before plugin creation, and we can assume it comes from a loaded project and there was a previous shader compilation => no need to trigger a render, and we can hide the number of params, the param type, and the param name. We can also close the "Image Shader" group.
void
ShadertoyPlugin::updateExtra()
{
    {
        AutoMutex lock( _imageShaderMutex.get() );
        // only do this if parameters were updated!
        if (_imageShaderUpdateParams) {
            _imageShaderUpdateParams = false;
            bool uniformsChanged = false;
            beginEditBlock(kParamAuto);
            // Try to avoid setting parameters to the same value, since this maytrigger an unnecessary instancechanged on some hosts
            for (unsigned i = 0; i < NBINPUTS; ++i) {
                if ( _imageShaderInputEnabled[i] != _inputEnable[i]->getValue() ) {
                    _inputEnable[i]->setValue(_imageShaderInputEnabled[i]);
                }
                string s;
                _inputLabel[i]->getValue(s);
                if (_imageShaderInputLabel[i] != s) {
                    _inputLabel[i]->setValue(_imageShaderInputLabel[i]);
                }
                _inputHint[i]->getValue(s);
                if (_imageShaderInputHint[i] != s) {
                    _inputHint[i]->setValue(_imageShaderInputHint[i]);
                }
                if ( _imageShaderInputFilter[i] != _inputFilter[i]->getValue() ) {
                    _inputFilter[i]->setValue(_imageShaderInputFilter[i]);
                }
                if ( _imageShaderInputWrap[i] != _inputWrap[i]->getValue() ) {
                    _inputWrap[i]->setValue(_imageShaderInputWrap[i]);
                }
            }
            if ( _imageShaderHasMouse != _mouseParams->getValue() ) {
                _mouseParams->setValue(_imageShaderHasMouse);
            }
            unsigned paramCount = std::min( _imageShaderExtraParameters.size() , _paramType.size() );
            if ( (int)paramCount != _paramCount->getValue() ) {
                _paramCount->setValue(paramCount);
                uniformsChanged = true;
            }
            for (unsigned i = 0; i < paramCount; ++i) {
                const ExtraParameter& p = _imageShaderExtraParameters[i];
                UniformTypeEnum t = p.getType();
                bool nChanged = false; // did the param name change? (required shader recompilation to get the uniform address)
                bool tChanged = ( t != (UniformTypeEnum)_paramType[i]->getValue() );
                if (tChanged) {
                    _paramType[i]->setValue( (int)t );
                }
                string s;
                _paramName[i]->getValue(s);
                if (p.getName() != s) {
                    _paramName[i]->setValue( p.getName() );
                    nChanged = true;
                }
                _paramLabel[i]->getValue(s);
                if (p.getLabel() != s) {
                    _paramLabel[i]->setValue( p.getLabel() );
                }
                _paramHint[i]->getValue(s);
                if (p.getHint() != s) {
                    _paramHint[i]->setValue( p.getHint() );
                }
                uniformsChanged |= (tChanged || nChanged);
                switch (t) {
                case eUniformTypeNone: {
                    if (tChanged) {
                        _paramDefaultBool[i]->resetToDefault();
                        _paramDefaultInt[i]->resetToDefault();
                        _paramMinInt[i]->resetToDefault();
                        _paramMaxInt[i]->resetToDefault();
                        _paramDefaultFloat[i]->resetToDefault();
                        _paramMinFloat[i]->resetToDefault();
                        _paramMaxFloat[i]->resetToDefault();
                        _paramDefaultVec2[i]->resetToDefault();
                        _paramMinVec2[i]->resetToDefault();
                        _paramMaxVec2[i]->resetToDefault();
                        _paramDefaultVec3[i]->resetToDefault();
                        //_paramMinVec3[i]->resetToDefault();
                        //_paramMaxVec3[i]->resetToDefault();
                        _paramDefaultVec4[i]->resetToDefault();
                        //_paramMinVec4[i]->resetToDefault();
                        //_paramMaxVec4[i]->resetToDefault();
                    }
                }
                case eUniformTypeBool: {
                    if (tChanged) {
                        //_paramDefaultBool[i]->resetToDefault();
                        _paramDefaultInt[i]->resetToDefault();
                        _paramMinInt[i]->resetToDefault();
                        _paramMaxInt[i]->resetToDefault();
                        _paramDefaultFloat[i]->resetToDefault();
                        _paramMinFloat[i]->resetToDefault();
                        _paramMaxFloat[i]->resetToDefault();
                        _paramDefaultVec2[i]->resetToDefault();
                        _paramMinVec2[i]->resetToDefault();
                        _paramMaxVec2[i]->resetToDefault();
                        _paramDefaultVec3[i]->resetToDefault();
                        //_paramMinVec3[i]->resetToDefault();
                        //_paramMaxVec3[i]->resetToDefault();
                        _paramDefaultVec4[i]->resetToDefault();
                        //_paramMinVec4[i]->resetToDefault();
                        //_paramMaxVec4[i]->resetToDefault();
                    }
                    _paramDefaultBool[i]->setValue(p.getDefault().b);
                    break;
                }
                case eUniformTypeInt: {
                    if (tChanged) {
                        _paramDefaultBool[i]->resetToDefault();
                        //_paramDefaultInt[i]->resetToDefault();
                        //_paramMinInt[i]->resetToDefault();
                        //_paramMaxInt[i]->resetToDefault();
                        _paramDefaultFloat[i]->resetToDefault();
                        _paramMinFloat[i]->resetToDefault();
                        _paramMaxFloat[i]->resetToDefault();
                        _paramDefaultVec2[i]->resetToDefault();
                        _paramMinVec2[i]->resetToDefault();
                        _paramMaxVec2[i]->resetToDefault();
                        _paramDefaultVec3[i]->resetToDefault();
                        //_paramMinVec3[i]->resetToDefault();
                        //_paramMaxVec3[i]->resetToDefault();
                        _paramDefaultVec4[i]->resetToDefault();
                        //_paramMinVec4[i]->resetToDefault();
                        //_paramMaxVec4[i]->resetToDefault();
                    }
                    _paramDefaultInt[i]->setValue(p.getDefault().i);
                    _paramMinInt[i]->setValue(p.getMin().i);
                    _paramMaxInt[i]->setValue(p.getMax().i);
                    break;
                }
                case eUniformTypeFloat: {
                    if (tChanged) {
                        _paramDefaultBool[i]->resetToDefault();
                        _paramDefaultInt[i]->resetToDefault();
                        _paramMinInt[i]->resetToDefault();
                        _paramMaxInt[i]->resetToDefault();
                        //_paramDefaultFloat[i]->resetToDefault();
                        //_paramMinFloat[i]->resetToDefault();
                        //_paramMaxFloat[i]->resetToDefault();
                        _paramDefaultVec2[i]->resetToDefault();
                        _paramMinVec2[i]->resetToDefault();
                        _paramMaxVec2[i]->resetToDefault();
                        _paramDefaultVec3[i]->resetToDefault();
                        //_paramMinVec3[i]->resetToDefault();
                        //_paramMaxVec3[i]->resetToDefault();
                        _paramDefaultVec4[i]->resetToDefault();
                        //_paramMinVec4[i]->resetToDefault();
                        //_paramMaxVec4[i]->resetToDefault();
                    }
                    _paramDefaultFloat[i]->setValue(p.getDefault().f[0]);
                    _paramMinFloat[i]->setValue(p.getMin().f[0]);
                    _paramMaxFloat[i]->setValue(p.getMax().f[0]);
                    break;
                }
                case eUniformTypeVec2: {
                    if (tChanged) {
                        _paramDefaultBool[i]->resetToDefault();
                        _paramDefaultInt[i]->resetToDefault();
                        _paramMinInt[i]->resetToDefault();
                        _paramMaxInt[i]->resetToDefault();
                        _paramDefaultFloat[i]->resetToDefault();
                        _paramMinFloat[i]->resetToDefault();
                        _paramMaxFloat[i]->resetToDefault();
                        //_paramDefaultVec2[i]->resetToDefault();
                        //_paramMinVec2[i]->resetToDefault();
                        //_paramMaxVec2[i]->resetToDefault();
                        _paramDefaultVec3[i]->resetToDefault();
                        //_paramMinVec3[i]->resetToDefault();
                        //_paramMaxVec3[i]->resetToDefault();
                        _paramDefaultVec4[i]->resetToDefault();
                        //_paramMinVec4[i]->resetToDefault();
                        //_paramMaxVec4[i]->resetToDefault();
                    }
                    _paramDefaultVec2[i]->setValue(p.getDefault().f[0], p.getDefault().f[1]);
                    _paramMinVec2[i]->setValue(p.getMin().f[0], p.getMin().f[1]);
                    _paramMaxVec2[i]->setValue(p.getMax().f[0], p.getMax().f[1]);
                    break;
                }
                case eUniformTypeVec3: {
                    if (tChanged) {
                        _paramDefaultBool[i]->resetToDefault();
                        _paramDefaultInt[i]->resetToDefault();
                        _paramMinInt[i]->resetToDefault();
                        _paramMaxInt[i]->resetToDefault();
                        _paramDefaultFloat[i]->resetToDefault();
                        _paramMinFloat[i]->resetToDefault();
                        _paramMaxFloat[i]->resetToDefault();
                        _paramDefaultVec2[i]->resetToDefault();
                        _paramMinVec2[i]->resetToDefault();
                        _paramMaxVec2[i]->resetToDefault();
                        //_paramDefaultVec3[i]->resetToDefault();
                        ////_paramMinVec3[i]->resetToDefault();
                        ////_paramMaxVec3[i]->resetToDefault();
                        _paramDefaultVec4[i]->resetToDefault();
                        //_paramMinVec4[i]->resetToDefault();
                        //_paramMaxVec4[i]->resetToDefault();
                    }
                    _paramDefaultVec3[i]->setValue(p.getDefault().f[0], p.getDefault().f[1], p.getDefault().f[2]);
                    //_paramMinVec3[i]->setValue(p.getMin().f[0], p.getMin().f[1], p.getMin().f[2])
                    //_paramMaxVec3[i]->setValue(p.getMax().f[0], p.getMax().f[1], p.getMax().f[2]);
                    break;
                }
                case eUniformTypeVec4: {
                    if (tChanged) {
                        _paramDefaultBool[i]->resetToDefault();
                        _paramDefaultInt[i]->resetToDefault();
                        _paramMinInt[i]->resetToDefault();
                        _paramMaxInt[i]->resetToDefault();
                        _paramDefaultFloat[i]->resetToDefault();
                        _paramMinFloat[i]->resetToDefault();
                        _paramMaxFloat[i]->resetToDefault();
                        _paramDefaultVec2[i]->resetToDefault();
                        _paramMinVec2[i]->resetToDefault();
                        _paramMaxVec2[i]->resetToDefault();
                        _paramDefaultVec3[i]->resetToDefault();
                        //_paramMinVec3[i]->resetToDefault();
                        //_paramMaxVec3[i]->resetToDefault();
                        //_paramDefaultVec4[i]->resetToDefault();
                        ////_paramMinVec4[i]->resetToDefault();
                        ////_paramMaxVec4[i]->resetToDefault();
                    }
                    _paramDefaultVec4[i]->setValue(p.getDefault().f[0], p.getDefault().f[1], p.getDefault().f[2], p.getDefault().f[3]);
                    //_paramMinVec4[i]->setValue(p.getMin().f[0], p.getMin().f[1], p.getMin().f[2], p.getMin().f[3]);
                    //_paramMaxVec4[i]->setValue(p.getMax().f[0], p.getMax().f[1], p.getMax().f[2], p.getMax().f[3]);
                    break;
                }
                default: {
                    assert(false);
                    if (tChanged) {
                        _paramDefaultBool[i]->resetToDefault();
                        _paramDefaultInt[i]->resetToDefault();
                        _paramMinInt[i]->resetToDefault();
                        _paramMaxInt[i]->resetToDefault();
                        _paramDefaultFloat[i]->resetToDefault();
                        _paramMinFloat[i]->resetToDefault();
                        _paramMaxFloat[i]->resetToDefault();
                        _paramDefaultVec2[i]->resetToDefault();
                        _paramMinVec2[i]->resetToDefault();
                        _paramMaxVec2[i]->resetToDefault();
                        _paramDefaultVec3[i]->resetToDefault();
                        //_paramMinVec3[i]->resetToDefault();
                        //_paramMaxVec3[i]->resetToDefault();
                        _paramDefaultVec4[i]->resetToDefault();
                        //_paramMinVec4[i]->resetToDefault();
                        //_paramMaxVec4[i]->resetToDefault();
                    }
                }
                } // switch
            }
            for (unsigned i = _imageShaderExtraParameters.size(); i < _paramType.size(); ++i) {
                bool tChanged = ( (UniformTypeEnum)_paramType[i]->getValue() != eUniformTypeNone );
                if (tChanged) {
                    _paramDefaultBool[i]->resetToDefault();
                    _paramDefaultInt[i]->resetToDefault();
                    _paramMinInt[i]->resetToDefault();
                    _paramMaxInt[i]->resetToDefault();
                    _paramDefaultFloat[i]->resetToDefault();
                    _paramMinFloat[i]->resetToDefault();
                    _paramMaxFloat[i]->resetToDefault();
                    _paramDefaultVec2[i]->resetToDefault();
                    _paramMinVec2[i]->resetToDefault();
                    _paramMaxVec2[i]->resetToDefault();
                    _paramDefaultVec3[i]->resetToDefault();
                    //_paramMinVec3[i]->resetToDefault();
                    //_paramMaxVec3[i]->resetToDefault();
                    _paramDefaultVec4[i]->resetToDefault();
                    //_paramMinVec4[i]->resetToDefault();
                    //_paramMaxVec4[i]->resetToDefault();
                }
            }
            _bbox->setValue( (int)_imageShaderBBox );
            resetParamsValues();
            endEditBlock();
            if (uniformsChanged) {
                // mark that image shader must be recompiled on next render
                ++_imageShaderUniformsID;
            }
        } // if (_imageShaderUpdateParams)
    }

    // update GUI
    unsigned paramCount = std::max( 0, std::min(_paramCount->getValue(), (int)_paramType.size()) );

    for (unsigned i = 0; i < paramCount; ++i) {
        UniformTypeEnum t = (UniformTypeEnum)_paramType[i]->getValue();
        if (t == eUniformTypeNone) {
            continue;
        }
        string name;
        string label;
        string hint;
        _paramName[i]->getValue(name);
        _paramLabel[i]->getValue(label);
        _paramHint[i]->getValue(hint);
        if ( label.empty() ) {
            label = name;
        }
#if 0
        if ( !name.empty() ) {
            if ( !hint.empty() ) {
                hint += '\n';
            }
            hint += "This parameter corresponds to 'uniform ";
            hint += mapUniformTypeToStr(t);
            hint += " " + name + "'.";
        }
#endif
        if ( name.empty() ) {
            _paramGroup[i]->setLabel( kGroupParameterLabel + unsignedToString(i) );
        } else {
            _paramGroup[i]->setLabel(name);
        }
        switch (t) {
        case eUniformTypeBool: {
            if ( !label.empty() ) {
                _paramValueBool[i]->setLabel(label);
            }
            if ( !hint.empty() ) {
                _paramValueBool[i]->setHint(hint);
            }
            bool v;
            _paramDefaultBool[i]->getValue(v);
            _paramValueBool[i]->setDefault(v);
            break;
        }
        case eUniformTypeInt: {
            if ( !label.empty() ) {
                _paramValueInt[i]->setLabel(label);
            }
            if ( !hint.empty() ) {
                _paramValueInt[i]->setHint(hint);
            }
            int v;
            _paramDefaultInt[i]->getValue(v);
            int vmin, vmax;
            _paramMinInt[i]->getValue(vmin);
            _paramMaxInt[i]->getValue(vmax);
            _paramValueInt[i]->setDefault(v);
            _paramValueInt[i]->setRange(vmin, vmax);
            _paramValueInt[i]->setDisplayRange(vmin, vmax);
            break;
        }
        case eUniformTypeFloat: {
            if ( !label.empty() ) {
                _paramValueFloat[i]->setLabel(label);
            }
            if ( !hint.empty() ) {
                _paramValueFloat[i]->setHint(hint);
            }
            double v;
            _paramDefaultFloat[i]->getValue(v);
            double vmin, vmax;
            _paramMinFloat[i]->getValue(vmin);
            _paramMaxFloat[i]->getValue(vmax);
            _paramValueFloat[i]->setDefault(v);
            _paramValueFloat[i]->setRange(vmin, vmax);
            _paramValueFloat[i]->setDisplayRange(vmin, vmax);
            break;
        }
        case eUniformTypeVec2: {
            if ( !label.empty() ) {
                _paramValueVec2[i]->setLabel(label);
            }
            if ( !hint.empty() ) {
                _paramValueVec2[i]->setHint(hint);
            }
            double v0, v1;
            _paramDefaultVec2[i]->getValue(v0, v1);
            double v0min, v1min, v0max, v1max;
            _paramMinVec2[i]->getValue(v0min, v1min);
            _paramMaxVec2[i]->getValue(v0max, v1max);
            _paramValueVec2[i]->setDefault(v0, v1);
            _paramValueVec2[i]->setRange(v0min, v1min, v0max, v1max);
            _paramValueVec2[i]->setDisplayRange(v0min, v1min, v0max, v1max);
            break;
        }
        case eUniformTypeVec3: {
            if ( !label.empty() ) {
                _paramValueVec3[i]->setLabel(label);
            }
            if ( !hint.empty() ) {
                _paramValueVec3[i]->setHint(hint);
            }
            double v0, v1, v2;
            _paramDefaultVec3[i]->getValue(v0, v1, v2);
            //double v0min, v1min, v2min, v0max, v1max, v2max;
            //_paramMinVec3[i]->getValue(v0min, v1min, v2min);
            //_paramMaxVec3[i]->getValue(v0max, v1max, v2max);
            _paramValueVec3[i]->setDefault(v0, v1, v2);
            //_paramValueVec3[i]->setRange(v0min, v1min, v2min, v0max, v1max, v2max);
            //_paramValueVec3[i]->setDisplayRange(v0min, v1min, v2min, v0max, v1max, v2max);
            break;
        }
        case eUniformTypeVec4: {
            if ( !label.empty() ) {
                _paramValueVec4[i]->setLabel(label);
            }
            if ( !hint.empty() ) {
                _paramValueVec4[i]->setHint(hint);
            }
            double v0, v1, v2, v3;
            _paramDefaultVec4[i]->getValue(v0, v1, v2, v3);
            //double v0min, v1min, v2min, v0max, v1max, v2max;
            //_paramMinVec4[i]->getValue(v0min, v1min, v2min);
            //_paramMaxVec4[i]->getValue(v0max, v1max, v2max);
            _paramValueVec4[i]->setDefault(v0, v1, v2, v3);
            //_paramValueVec4[i]->setRange(v0min, v1min, v2min, v0max, v1max, v2max);
            //_paramValueVec4[i]->setDisplayRange(v0min, v1min, v2min, v0max, v1max, v2max);
            break;
        }
        default:
            assert(false);
            break;
        } // switch
    }
} // ShadertoyPlugin::updateExtra

// reset the extra parameters to their default value
void
ShadertoyPlugin::resetParamsValues()
{
    //beginEditBlock(kParamResetParams);
    unsigned paramCount = std::max( 0, std::min(_paramCount->getValue(), (int)_paramType.size()) );
    for (unsigned i = 0; i < paramCount; ++i) {
        UniformTypeEnum t = (UniformTypeEnum)_paramType[i]->getValue();
        if (t == eUniformTypeNone) {
            continue;
        }
        switch (t) {
        case eUniformTypeBool: {
            bool v;
            _paramDefaultBool[i]->getValue(v);
            _paramValueBool[i]->setValue(v);
            break;
        }
        case eUniformTypeInt: {
            int v;
            _paramDefaultInt[i]->getValue(v);
            _paramValueInt[i]->setValue(v);
            break;
        }
        case eUniformTypeFloat: {
            double v;
            _paramDefaultFloat[i]->getValue(v);
            _paramValueFloat[i]->setValue(v);
            break;
        }
        case eUniformTypeVec2: {
            double v0, v1;
            _paramDefaultVec2[i]->getValue(v0, v1);
            _paramValueVec2[i]->setValue(v0, v1);
            break;
        }
        case eUniformTypeVec3: {
            double v0, v1, v2;
            _paramDefaultVec3[i]->getValue(v0, v1, v2);
            _paramValueVec3[i]->setValue(v0, v1, v2);
            break;
        }
        case eUniformTypeVec4: {
            double v0, v1, v2, v3;
            _paramDefaultVec4[i]->getValue(v0, v1, v2, v3);
            _paramValueVec4[i]->setValue(v0, v1, v2, v3);
            break;
        }
        default:
            assert(false);
            break;
        }
    }
    //endEditBlock();
} // ShadertoyPlugin::resetParamsValues

void
ShadertoyPlugin::changedParam(const InstanceChangedArgs &args,
                              const string &paramName)
{
    const double time = args.time;

    if ( (paramName == kParamBBox) && (args.reason == eChangeUserEdit) ) {
        updateVisibility();
    } else if (paramName == kParamFormat) {
        //the host does not handle the format itself, do it ourselves
        EParamFormat format = (EParamFormat)_format->getValueAtTime(time);
        int w = 0, h = 0;
        double par = -1;
        getFormatResolution(format, &w, &h, &par);
        assert(par != -1);
        _formatPar->setValue(par);
        _formatSize->setValue(w, h);
    } else if ( (paramName == kParamImageShaderFileName) ||
                ( paramName == kParamImageShaderReload) ) {
        // load image shader from file
        string imageShaderFileName;
        _imageShaderFileName->getValueAtTime(time, imageShaderFileName);
        if ( !imageShaderFileName.empty() ) {
            std::ifstream t( imageShaderFileName.c_str() );
            if ( t.bad() ) {
                sendMessage(Message::eMessageError, "", string("Error: Cannot open file ") + imageShaderFileName);
            } else {
                string str;
                t.seekg(0, std::ios::end);
                str.reserve( t.tellg() );
                t.seekg(0, std::ios::beg);
                str.assign( ( std::istreambuf_iterator<char>(t) ),
                            std::istreambuf_iterator<char>() );
                _imageShaderSource->setValue(str);
            }
        }
    } else if (paramName == kParamImageShaderPresetDir) {
        string dir;
        _imageShaderPresetDir->getValue(dir);
        presetsFromDir(dir, _presets);
        _imageShaderPreset->resetOptions();
        _imageShaderPreset->appendOption("No preset");
        for (std::vector<ShadertoyPlugin::Preset>::iterator it = _presets.begin(); it != _presets.end(); ++it) {
            _imageShaderPreset->appendOption(it->description);
        }
    } else if (paramName == kParamImageShaderPreset) {
        int preset = _imageShaderPreset->getValue() - 1;
        if ( preset >= 0 && preset < (int)_presets.size() ) {
            // load image shader from file
            string imageShaderFileName = _presets[preset].filename;
            if ( !imageShaderFileName.empty() ) {
                std::ifstream t( imageShaderFileName.c_str() );
                if ( t.bad() ) {
                    sendMessage(Message::eMessageError, "", string("Error: Cannot open file ") + imageShaderFileName);
                } else {
                    string str;
                    t.seekg(0, std::ios::end);
                    str.reserve( t.tellg() );
                    t.seekg(0, std::ios::beg);
                    str.assign( ( std::istreambuf_iterator<char>(t) ),
                               std::istreambuf_iterator<char>() );
                    _imageShaderSource->setValue(str);
                }
            }
            // same as kParamImageShaderCompile below, except ask for param update
            {
                AutoMutex lock( _imageShaderMutex.get() );
                // mark that image shader must be recompiled on next render
                ++_imageShaderID;
                _imageShaderUpdateParams = true;
                _imageShaderCompiled = false;
            }
            _imageShaderCompile->setEnabled(false);
            // trigger a new render which updates params and inputs info
            clearPersistentMessage();
            _imageShaderTriggerRender->setValue(_imageShaderTriggerRender->getValueAtTime(time) + 1);
        }

    } else if (paramName == kParamImageShaderCompile) {
        {
            AutoMutex lock( _imageShaderMutex.get() );
            // mark that image shader must be recompiled on next render
            ++_imageShaderID;
            _imageShaderUpdateParams = false;
            _imageShaderCompiled = false;
        }
        _imageShaderCompile->setEnabled(false);
        // trigger a new render
        clearPersistentMessage();
        _imageShaderTriggerRender->setValue(_imageShaderTriggerRender->getValueAtTime(time) + 1);
    } else if ( (paramName == kParamAuto) || (paramName == kParamImageShaderParamsUpdated) ) {
        bool recompile = true;
        {
            AutoMutex lock( _imageShaderMutex.get() );
            if (_imageShaderUpdateParams && _imageShaderCompiled) {
                _imageShaderCompiled = false; //_imageShaderUpdateParams is reset by updateExtra()
                recompile = false; // parameters were updated (second click in a host that doesn't support setValue() from render(), probably), we just need to update the Gui
            } else {
                // same as kParamImageShaderCompile above, except ask for param update
                // mark that image shader must be recompiled on next render
                ++_imageShaderID;
                _imageShaderUpdateParams = true;
                _imageShaderCompiled = false;
            }
        }
        if (recompile) {
            // same as kParamImageShaderCompile above
            _imageShaderCompile->setEnabled(false);
            // trigger a new render which updates params and inputs info
            clearPersistentMessage();
            _imageShaderTriggerRender->setValue(_imageShaderTriggerRender->getValueAtTime(time) + 1);
        } else {
            updateExtra();
            updateVisibility();
            updateClips();
        }
    } else if (paramName == kParamResetParams) {
        beginEditBlock(kParamResetParams);
        resetParamsValues();
        endEditBlock();
    } else if (paramName == kParamImageShaderSource) {
        _imageShaderCompile->setEnabled(true);
        if (args.reason == eChangeUserEdit) {
            _imageShaderPreset->setValue(0);
        }
    } else if ( ( (paramName == kParamCount) ||
                  starts_with(paramName, kParamName) ) && (args.reason == eChangeUserEdit) ) {
        {
            AutoMutex lock( _imageShaderMutex.get() );
            // mark that image shader must be recompiled on next render
            ++_imageShaderUniformsID;
        }
        //updateExtra();
        updateVisibility();
    } else if (paramName == kParamMouseParams) {
        updateVisibility();
    } else if ( starts_with(paramName, kParamType) && (args.reason == eChangeUserEdit) ) {
        {
            AutoMutex lock( _imageShaderMutex.get() );
            // mark that image shader must be recompiled on next render
            ++_imageShaderUniformsID;
        }
        //updateVisibilityParam(i, i < paramCount);
        updateVisibility();
    } else if ( ( starts_with(paramName, kParamName) ||
                  starts_with(paramName, kParamLabel) ||
                  starts_with(paramName, kParamHint) ||
                  starts_with(paramName, kParamDefault) ||
                  starts_with(paramName, kParamMin) ||
                  starts_with(paramName, kParamMax) ) && (args.reason == eChangeUserEdit) ) {
        updateExtra();
    } else if ( ( starts_with(paramName, kParamInputLabel) ||
                  starts_with(paramName, kParamInputHint) ) && (args.reason == eChangeUserEdit) ) {
        updateClips();
    } else if ( starts_with(paramName, kParamInputEnable) && (args.reason == eChangeUserEdit) ) {
        updateClips();
        updateVisibility();
    } else if ( (paramName == kParamImageShaderSource) && (args.reason == eChangeUserEdit) ) {
        _imageShaderCompile->setEnabled(true);
    } else if (paramName == kParamRendererInfo) {
        string message;
        {
            AutoMutex lock( _rendererInfoMutex.get() );
            message = _rendererInfo;
        }
        if ( message.empty() ) {
            sendMessage(Message::eMessageMessage, "", "OpenGL renderer info not yet available.\n"
                        "Please execute at least one image render and try again.");
        } else {
            sendMessage(Message::eMessageMessage, "", message);
        }
    } else if (paramName == kParamHelp) {
        sendMessage(Message::eMessageMessage, "", kPluginDescription);
#if defined(HAVE_OSMESA)
    } else if (paramName == kParamEnableGPU) {
        setSupportsOpenGLRender( _enableGPU->getValueAtTime(args.time) );
        {
            AutoMutex lock( _rendererInfoMutex.get() );
            _rendererInfo.clear();
        }
    } else if (paramName == kParamCPUDriver) {
        {
            AutoMutex lock( _rendererInfoMutex.get() );
            _rendererInfo.clear();
        }
#endif
    }
} // ShadertoyPlugin::changedParam

mDeclarePluginFactory(ShadertoyPluginFactory, {ofxsThreadSuiteCheck();}, {});
#if 0
void
ShadertoyPluginFactory::load()
{
    // we can't be used on hosts that don't support the OpenGL suite
    // returning an error here causes a blank menu entry in Nuke
    //#if defined(OFX_SUPPORTS_OPENGLRENDER) && !defined(HAVE_OSMESA)
    //const ImageEffectHostDescription &gHostDescription = *getImageEffectHostDescription();
    //if (!gHostDescription.supportsOpenGLRender) {
    //    throwHostMissingSuiteException(kOfxOpenGLRenderSuite);
    //}
    //#endif
}
#endif

void
ShadertoyPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // returning an error here crashes Nuke
    //#if defined(OFX_SUPPORTS_OPENGLRENDER) && !defined(HAVE_OSMESA)
    //const ImageEffectHostDescription &gHostDescription = *getImageEffectHostDescription();
    //if (!gHostDescription.supportsOpenGLRender) {
    //    throwHostMissingSuiteException(kOfxOpenGLRenderSuite);
    //}
    //#endif

    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    if ( desc.getPropertySet().propGetDimension(kNatronOfxPropDescriptionIsMarkdown, false) ) {
        desc.setPluginDescription(kPluginDescriptionMarkdown, false);
        desc.setDescriptionIsMarkdown(true);
    } else {
        desc.setPluginDescription(kPluginDescription);
    }

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
    const ImageEffectHostDescription &gHostDescription = *getImageEffectHostDescription();
    if (!gHostDescription.supportsOpenGLRender) {
        throwSuiteStatusException(kOfxStatErrMissingHostFeature);
    }
#endif
#endif
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif

    // some hosts may have the multithread suite, but no mutex capability (e.g. Sony Catalyst)
    try {
        ShadertoyPlugin::Mutex m;
        desc.setRenderThreadSafety(eRenderFullySafe);
    } catch (const std::exception &e) {
#      ifdef DEBUG
        std::cout << "ERROR in describe(): Mutex creation returned " << e.what() << std::endl;
#      endif
        desc.setRenderThreadSafety(eRenderInstanceSafe);
    }
} // ShadertoyPluginFactory::describe

static void
defineBooleanSub(ImageEffectDescriptor &desc,
                 const string& nb,
                 const string& name,
                 const string& label,
                 const string& hint,
                 bool isExtraParam,
                 PageParamDescriptor *page,
                 GroupParamDescriptor *group)
{
    BooleanParamDescriptor* param = desc.defineBooleanParam(name + nb);

    param->setLabel(label + nb);
    param->setHint(hint);
    param->setEvaluateOnChange(isExtraParam);
    param->setAnimates(isExtraParam);
    if (page) {
        page->addChild(*param);
    }
    if (group) {
        param->setParent(*group);
    }
}

static void
defineBoolean(ImageEffectDescriptor &desc,
              const string& nb,
              PageParamDescriptor *page,
              GroupParamDescriptor *group)
{
    defineBooleanSub(desc, nb, kParamDefaultBool, kParamDefaultLabel, kParamDefaultHint, false, page, group);
}

static void
defineIntSub(ImageEffectDescriptor &desc,
             const string& nb,
             const string& name,
             const string& label,
             const string& hint,
             bool isExtraParam,
             int defaultValue,
             PageParamDescriptor *page,
             GroupParamDescriptor *group)
{
    IntParamDescriptor* param = desc.defineIntParam(name + nb);

    param->setLabel(label + nb);
    param->setHint(hint);
    param->setRange(INT_MIN, INT_MAX);
    param->setDisplayRange(INT_MIN, INT_MAX);
    param->setDefault(defaultValue);
    param->setEvaluateOnChange(isExtraParam);
    param->setAnimates(isExtraParam);
    if (page) {
        page->addChild(*param);
    }
    if (group) {
        param->setParent(*group);
    }
}

static void
defineInt(ImageEffectDescriptor &desc,
          const string& nb,
          PageParamDescriptor *page,
          GroupParamDescriptor *group)
{
    defineIntSub(desc, nb, kParamDefaultInt, kParamDefaultLabel, kParamDefaultHint, false, 0, page, group);
    defineIntSub(desc, nb, kParamMinInt, kParamMinLabel, kParamMinHint, false, INT_MIN, page, group);
    defineIntSub(desc, nb, kParamMaxInt, kParamMaxLabel, kParamMaxHint, false, INT_MAX, page, group);
}

static void
defineDoubleSub(ImageEffectDescriptor &desc,
                const string& nb,
                const string& name,
                const string& label,
                const string& hint,
                bool isExtraParam,
                double defaultValue,
                PageParamDescriptor *page,
                GroupParamDescriptor *group)
{
    DoubleParamDescriptor* param = desc.defineDoubleParam(name + nb);

    param->setLabel(label + nb);
    param->setHint(hint);
    param->setRange(-DBL_MAX, DBL_MAX);
    param->setDisplayRange(-DBL_MAX, DBL_MAX);
    param->setDefault(defaultValue);
    param->setDoubleType(eDoubleTypePlain);
    param->setEvaluateOnChange(isExtraParam);
    param->setAnimates(isExtraParam);
    if (page) {
        page->addChild(*param);
    }
    if (group) {
        param->setParent(*group);
    }
}

static void
defineDouble(ImageEffectDescriptor &desc,
             const string& nb,
             PageParamDescriptor *page,
             GroupParamDescriptor *group)
{
    defineDoubleSub(desc, nb, kParamDefaultFloat, kParamDefaultLabel, kParamDefaultHint, false, 0, page, group);
    defineDoubleSub(desc, nb, kParamMinFloat, kParamMinLabel, kParamMinHint, false, -DBL_MAX, page, group);
    defineDoubleSub(desc, nb, kParamMaxFloat, kParamMaxLabel, kParamMaxHint, false, DBL_MAX, page, group);
}

static void
defineDouble2DSub(ImageEffectDescriptor &desc,
                  const string& nb,
                  const string& name,
                  const string& label,
                  const string& hint,
                  bool isExtraParam,
                  double defaultValue,
                  PageParamDescriptor *page,
                  GroupParamDescriptor *group)
{
    Double2DParamDescriptor* param = desc.defineDouble2DParam(name + nb);

    param->setLabel(label + nb);
    param->setHint(hint);
    param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX);
    param->setDisplayRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX);
    param->setDefault(defaultValue, defaultValue);
    param->setDoubleType(eDoubleTypePlain);
    param->setEvaluateOnChange(isExtraParam);
    param->setAnimates(isExtraParam);
    if (page) {
        page->addChild(*param);
    }
    if (group) {
        param->setParent(*group);
    }
}

static void
defineDouble2D(ImageEffectDescriptor &desc,
               const string& nb,
               PageParamDescriptor *page,
               GroupParamDescriptor *group)
{
    defineDouble2DSub(desc, nb, kParamDefaultVec2, kParamDefaultLabel, kParamDefaultHint, false, 0, page, group);
    defineDouble2DSub(desc, nb, kParamMinVec2, kParamMinLabel, kParamMinHint, false, -DBL_MAX, page, group);
    defineDouble2DSub(desc, nb, kParamMaxVec2, kParamMaxLabel, kParamMaxHint, false, DBL_MAX, page, group);
}

static void
defineDouble3DSub(ImageEffectDescriptor &desc,
                  const string& nb,
                  const string& name,
                  const string& label,
                  const string& hint,
                  bool isExtraParam,
                  double defaultValue,
                  PageParamDescriptor *page,
                  GroupParamDescriptor *group)
{
    RGBParamDescriptor* param = desc.defineRGBParam(name + nb);

    param->setLabel(label + nb);
    param->setHint(hint);
    param->setRange(-DBL_MAX, -DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX);
    param->setDisplayRange(-DBL_MAX, -DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX);
    param->setDefault(defaultValue, defaultValue, defaultValue);
    param->setEvaluateOnChange(isExtraParam);
    param->setAnimates(isExtraParam);
    if (page) {
        page->addChild(*param);
    }
    if (group) {
        param->setParent(*group);
    }
}

static void
defineDouble3D(ImageEffectDescriptor &desc,
               const string& nb,
               PageParamDescriptor *page,
               GroupParamDescriptor *group)
{
    defineDouble3DSub(desc, nb, kParamDefaultVec3, kParamDefaultLabel, kParamDefaultHint, false, 0, page, group);
    //defineDouble3DSub(desc, nb, kParamMinVec3, kParamMinLabel, kParamMinHint, false, -DBL_MAX, page, group);
    //defineDouble3DSub(desc, nb, kParamMaxVec3, kParamMaxLabel, kParamMaxHint, false, DBL_MAX, page, group);
}

static void
defineRGBASub(ImageEffectDescriptor &desc,
              const string& nb,
              const string& name,
              const string& label,
              const string& hint,
              bool isExtraParam,
              double defaultValue,
              PageParamDescriptor *page,
              GroupParamDescriptor *group)
{
    RGBAParamDescriptor* param = desc.defineRGBAParam(name + nb);

    param->setLabel(label + nb);
    param->setHint(hint);
    param->setRange(-DBL_MAX, -DBL_MAX, -DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX);
    param->setDisplayRange(-DBL_MAX, -DBL_MAX, -DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX);
    param->setDefault(defaultValue, defaultValue, defaultValue, defaultValue);
    param->setEvaluateOnChange(isExtraParam);
    param->setAnimates(isExtraParam);
    if (page) {
        page->addChild(*param);
    }
    if (group) {
        param->setParent(*group);
    }
}

static void
defineRGBA(ImageEffectDescriptor &desc,
           const string& nb,
           PageParamDescriptor *page,
           GroupParamDescriptor *group)
{
    defineRGBASub(desc, nb, kParamDefaultVec4, kParamDefaultLabel, kParamDefaultHint, false, 0, page, group);
    //defineRGBASub(desc, nb, kParamMinVec4, kParamMinLabel, kParamMinHint, false, -DBL_MAX, page, group);
    //defineRGBASub(desc, nb, kParamMaxVec4, kParamMaxLabel, kParamMaxHint, false, DBL_MAX, page, group);
}

void
ShadertoyPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                          ContextEnum context)
{
    const ImageEffectHostDescription &gHostDescription = *getImageEffectHostDescription();
#if defined(OFX_SUPPORTS_OPENGLRENDER) && !defined(HAVE_OSMESA)
    if (!gHostDescription.supportsOpenGLRender) {
        throwHostMissingSuiteException(kOfxOpenGLRenderSuite);
    }
#endif

    // parse the Shadertoy.txt file from the resources to fetch the presets
    string resourcesPath = desc.getPropertySet().propGetString(kOfxPluginPropFilePath, /*throwOnFailure=*/false) + "/Contents/Resources";
    presetsFromDir(resourcesPath + "/presets/default", gPresetsDefault);


    // Source clip only in the filter context
    // create the mandated source clip
    {
        ClipDescriptor *srcClip = desc.defineClip( (context == eContextFilter) ?
                                                   kOfxImageEffectSimpleSourceClipName :
                                                   kClipChannel "0" );
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentAlpha);
        srcClip->setTemporalClipAccess(false);
        srcClip->setSupportsTiles(kSupportsTiles);
        srcClip->setIsMask(false);
        srcClip->setOptional( !(context == eContextFilter) );
    }
    for (unsigned i = 1; i < NBINPUTS; ++i) {
        string iChannelX(kClipChannel);
        iChannelX += unsignedToString(i);
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
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamMousePosition);
        param->setLabel(kParamMousePositionLabel);
        param->setHint(kParamMousePositionHint);
        param->setDoubleType(eDoubleTypeXYAbsolute);
        param->setDefaultCoordinateSystem(eCoordinatesCanonical); // Nuke defaults to Normalized for XY and XYAbsolute!
        param->setUseHostNativeOverlayHandle(true);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamMouseClick);
        param->setLabel(kParamMouseClickLabel);
        param->setHint(kParamMouseClickHint);
        param->setDoubleType(eDoubleTypeXYAbsolute);
        param->setDefaultCoordinateSystem(eCoordinatesCanonical); // Nuke defaults to Normalized for XY and XYAbsolute!
        param->setDefault(1.,1.);
        param->setUseHostNativeOverlayHandle(true);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamMousePressed);
        param->setLabel(kParamMousePressedLabel);
        param->setHint(kParamMousePressedHint);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    const unsigned int nbuniforms = (gHostDescription.hostName == "uk.co.thefoundry.nuke" && gHostDescription.versionMajor == 7) ? SHADERTOY_NBUNIFORMS_NUKE7 : NBUNIFORMS; //if more than 7, Nuke 7's parameter page goes blank when unfolding the Extra Parameters group
    for (unsigned i = 0; i < nbuniforms; ++i) {
        // generate the number string
        string nb = unsignedToString(i);
        defineBooleanSub(desc, nb, kParamValueBool, kParamValueLabel, kParamValueHint, true, page, NULL);
        defineIntSub(desc, nb, kParamValueInt, kParamValueLabel, kParamValueHint, true, 0, page, NULL);
        defineDoubleSub(desc, nb, kParamValueFloat, kParamValueLabel, kParamValueHint, true, 0, page, NULL);
        defineDouble2DSub(desc, nb, kParamValueVec2, kParamValueLabel, kParamValueHint, true, 0, page, NULL);
        defineDouble3DSub(desc, nb, kParamValueVec3, kParamValueLabel, kParamValueHint, true, 0, page, NULL);
        defineRGBASub(desc, nb, kParamValueVec4, kParamValueLabel, kParamValueHint, true, 0, page, NULL);
    }

    {
        GroupParamDescriptor* group = desc.defineGroupParam(kGroupImageShader);
        if (group) {
            group->setLabel(kGroupImageShaderLabel);
            group->setOpen(false);
            //group->setAsTab();
            if (page) {
                page->addChild(*group);
            }
        }

        {
            StringParamDescriptor* param = desc.defineStringParam(kParamImageShaderFileName);
            param->setLabel(kParamImageShaderFileNameLabel);
            param->setHint(kParamImageShaderFileNameHint);
            param->setStringType(eStringTypeFilePath);
            param->setFilePathExists(true);
            param->setLayoutHint(eLayoutHintNoNewLine, 1);
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
            PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamImageShaderReload);
            param->setLabel(kParamImageShaderReloadLabel);
            param->setHint(kParamImageShaderReloadHint);
            if (page) {
                page->addChild(*param);
            }
            if (group) {
                param->setParent(*group);
            }
        }

        if ( !gPresetsDefault.empty() ) {
            StringParamDescriptor* param = desc.defineStringParam(kParamImageShaderPresetDir);
            param->setLabel(kParamImageShaderPresetDirLabel);
            string hint = kParamImageShaderPresetDirHint;
            replace(hint, "%1", resourcesPath + "/presets");
            param->setHint(hint);
            param->setStringType(eStringTypeDirectoryPath);
            param->setDefault(resourcesPath + "/presets/default");
            param->setEnabled(getImageEffectHostDescription()->isNatron);
            param->setFilePathExists(true);
            param->setEvaluateOnChange(false); // render is triggered using kParamImageShaderTriggerRender
            param->setAnimates(false);
            if (page) {
                page->addChild(*param);
            }
            if (group) {
                param->setParent(*group);
            }
        }
        if ( !gPresetsDefault.empty() ) {
            ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamImageShaderPreset);
            param->setLabel(kParamImageShaderPresetLabel);
            string hint = kParamImageShaderPresetHint;
            replace(hint, "%1", resourcesPath + "/presets");
            param->setHint(hint);
            param->setCascading(true);
            param->appendOption("No preset");
            for (std::vector<ShadertoyPlugin::Preset>::iterator it = gPresetsDefault.begin(); it != gPresetsDefault.end(); ++it) {
                param->appendOption(it->description);
            }
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
            StringParamDescriptor* param = desc.defineStringParam(kParamImageShaderSource);
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
            PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamImageShaderCompile);
            param->setLabel(kParamImageShaderCompileLabel);
            param->setHint(kParamImageShaderCompileHint);
            param->setLayoutHint(eLayoutHintNoNewLine, 1);
            if (page) {
                page->addChild(*param);
            }
            if (group) {
                param->setParent(*group);
            }
        }

        {
            PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamAuto);
            param->setLabel(kParamAutoLabel);
            param->setHint(kParamAutoHint);
            param->setLayoutHint(eLayoutHintNoNewLine, 1);
            if (page) {
                page->addChild(*param);
            }
            if (group) {
                param->setParent(*group);
            }
        }

        {
            PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamResetParams);
            param->setLabel(kParamResetParamsLabel);
            param->setHint(kParamResetParamsHint);
            if (page) {
                page->addChild(*param);
            }
            if (group) {
                param->setParent(*group);
            }
        }

        {
            // a dummy boolean parameter, used to trigger a new render when the shader is to be recompiled
            IntParamDescriptor* param = desc.defineIntParam(kParamImageShaderTriggerRender);
            param->setEvaluateOnChange(true);
            param->setAnimates(false);
            param->setIsSecretAndDisabled(true);
            param->setIsPersistent(false);
            if (page) {
                page->addChild(*param);
            }
            if (group) {
                param->setParent(*group);
            }
        }

        {
            // a dummy boolean parameter, used to update parameters GUI when the shader was recompiled
            BooleanParamDescriptor* param = desc.defineBooleanParam(kParamImageShaderParamsUpdated);
            param->setEvaluateOnChange(false);
            param->setAnimates(false);
            param->setIsSecretAndDisabled(true);
            param->setIsPersistent(false);
            if (page) {
                page->addChild(*param);
            }
            if (group) {
                param->setParent(*group);
            }
        }

        for (unsigned i = 0; i < NBINPUTS; ++i) {
            string nb = unsignedToString(i);
            {
                StringParamDescriptor* param = desc.defineStringParam(kParamInputName + nb);
                param->setLabel("");
                param->setDefault(kClipChannel + nb);
                param->setStringType(eStringTypeLabel);
                param->setLayoutHint(eLayoutHintNoNewLine, 1);
                if (page) {
                    page->addChild(*param);
                }
                if (group) {
                    param->setParent(*group);
                }
            }
            {
                BooleanParamDescriptor* param = desc.defineBooleanParam(kParamInputEnable + nb);
                param->setLabel(kParamInputEnableLabel);
                param->setHint(kParamInputEnableHint);
                param->setDefault(true);
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
                ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamInputFilter + nb);
                param->setLabel(kParamInputFilterLabel);
                param->setHint(kParamInputFilterHint);
                assert(param->getNOptions() == (int)ShadertoyPlugin::eFilterNearest);
                param->appendOption(kParamInputFilterOptionNearest);
                assert(param->getNOptions() == (int)ShadertoyPlugin::eFilterLinear);
                param->appendOption(kParamInputFilterOptionLinear);
                assert(param->getNOptions() == (int)ShadertoyPlugin::eFilterMipmap);
                param->appendOption(kParamInputFilterOptionMipmap);
                assert(param->getNOptions() == (int)ShadertoyPlugin::eFilterAnisotropic);
                param->appendOption(kParamInputFilterOptionAnisotropic);
                param->setDefault( (int)ShadertoyPlugin::eFilterMipmap );
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
                ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamInputWrap + nb);
                param->setLabelAndHint(kParamInputWrapLabel);
                assert(param->getNOptions() == (int)ShadertoyPlugin::eWrapRepeat);
                param->appendOption(kParamInputWrapOptionRepeat);
                assert(param->getNOptions() == (int)ShadertoyPlugin::eWrapClamp);
                param->appendOption(kParamInputWrapOptionClamp);
                assert(param->getNOptions() == (int)ShadertoyPlugin::eWrapMirror);
                param->appendOption(kParamInputWrapOptionMirror);
                param->setDefault( (int)ShadertoyPlugin::eWrapRepeat );
                param->setAnimates(false);
                if (page) {
                    page->addChild(*param);
                }
                if (group) {
                    param->setParent(*group);
                }
            }
            {
                StringParamDescriptor* param = desc.defineStringParam(kParamInputLabel + nb);
                param->setLabel(kParamInputLabelLabel);
                param->setHint(kParamInputLabelHint);
                param->setDefault("");
                param->setAnimates(false);
                if (page) {
                    page->addChild(*param);
                }
                if (group) {
                    param->setParent(*group);
                }
            }
            {
                StringParamDescriptor* param = desc.defineStringParam(kParamInputHint + nb);
                param->setLabel(kParamInputHintLabel);
                param->setDefault("");
                param->setAnimates(false);
                if (page) {
                    page->addChild(*param);
                }
                if (group) {
                    param->setParent(*group);
                }
            }
        }

        // boundingBox
        {
            ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamBBox);
            param->setLabel(kParamBBoxLabel);
            param->setHint(kParamBBoxHint);
            assert(param->getNOptions() == (int)ShadertoyPlugin::eBBoxDefault);
            param->appendOption(kParamBBoxOptionDefault);
            assert(param->getNOptions() == (int)ShadertoyPlugin::eBBoxFormat);
            param->appendOption(kParamBBoxOptionFormat);
            //assert(param->getNOptions() == (int)eBBoxSize);
            //param->appendOption(kParamBBoxOptionSize, kParamBBoxOptionSizeHint);
            assert(param->getNOptions() == (int)ShadertoyPlugin::eBBoxUnion);
            param->appendOption(kParamBBoxOptionUnion);
            assert(param->getNOptions() == (int)ShadertoyPlugin::eBBoxIntersection);
            param->appendOption(kParamBBoxOptionIntersection);
            assert(param->getNOptions() == (int)ShadertoyPlugin::eBBoxIChannel);
            for (unsigned i = 0; i < NBINPUTS; ++i) {
                string nb = unsignedToString(i);
                param->appendOption(string(kParamBBoxOptionIChannel) + nb, string(kParamBBoxOptionIChannelHint) + nb + '.', string(kParamBBoxOptionIChannelEnum) + nb);
            }
            param->setAnimates(true);
            param->setDefault( (int)ShadertoyPlugin::eBBoxDefault );
            if (page) {
                page->addChild(*param);
            }
            if (group) {
                param->setParent(*group);
            }
        }

        // format
        {
            ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamFormat);
            param->setLabel(kParamFormatLabel);
            assert(param->getNOptions() == eParamFormatPCVideo);
            param->appendOption(kParamFormatPCVideoLabel, "", kParamFormatPCVideo);
            assert(param->getNOptions() == eParamFormatNTSC);
            param->appendOption(kParamFormatNTSCLabel, "", kParamFormatNTSC);
            assert(param->getNOptions() == eParamFormatPAL);
            param->appendOption(kParamFormatPALLabel, "", kParamFormatPAL);
            assert(param->getNOptions() == eParamFormatNTSC169);
            param->appendOption(kParamFormatNTSC169Label, "", kParamFormatNTSC169);
            assert(param->getNOptions() == eParamFormatPAL169);
            param->appendOption(kParamFormatPAL169Label, "", kParamFormatPAL169);
            assert(param->getNOptions() == eParamFormatHD720);
            param->appendOption(kParamFormatHD720Label, "", kParamFormatHD720);
            assert(param->getNOptions() == eParamFormatHD);
            param->appendOption(kParamFormatHDLabel, "", kParamFormatHD);
            assert(param->getNOptions() == eParamFormatUHD4K);
            param->appendOption(kParamFormatUHD4KLabel, "", kParamFormatUHD4K);
            assert(param->getNOptions() == eParamFormat1kSuper35);
            param->appendOption(kParamFormat1kSuper35Label, "", kParamFormat1kSuper35);
            assert(param->getNOptions() == eParamFormat1kCinemascope);
            param->appendOption(kParamFormat1kCinemascopeLabel, "", kParamFormat1kCinemascope);
            assert(param->getNOptions() == eParamFormat2kSuper35);
            param->appendOption(kParamFormat2kSuper35Label, "", kParamFormat2kSuper35);
            assert(param->getNOptions() == eParamFormat2kCinemascope);
            param->appendOption(kParamFormat2kCinemascopeLabel, "", kParamFormat2kCinemascope);
            assert(param->getNOptions() == eParamFormat2kDCP);
            param->appendOption(kParamFormat2kDCPLabel, "", kParamFormat2kDCP);
            assert(param->getNOptions() == eParamFormat4kSuper35);
            param->appendOption(kParamFormat4kSuper35Label, "", kParamFormat4kSuper35);
            assert(param->getNOptions() == eParamFormat4kCinemascope);
            param->appendOption(kParamFormat4kCinemascopeLabel, "", kParamFormat4kCinemascope);
            assert(param->getNOptions() == eParamFormat4kDCP);
            param->appendOption(kParamFormat4kDCPLabel, "", kParamFormat4kDCP);
            assert(param->getNOptions() == eParamFormatSquare256);
            param->appendOption(kParamFormatSquare256Label, "", kParamFormatSquare256);
            assert(param->getNOptions() == eParamFormatSquare512);
            param->appendOption(kParamFormatSquare512Label, "", kParamFormatSquare512);
            assert(param->getNOptions() == eParamFormatSquare1k);
            param->appendOption(kParamFormatSquare1kLabel, "", kParamFormatSquare1k);
            assert(param->getNOptions() == eParamFormatSquare2k);
            param->appendOption(kParamFormatSquare2kLabel, "", kParamFormatSquare2k);
            param->setDefault(eParamFormatPCVideo);
            param->setHint(kParamFormatHint);
            param->setAnimates(false);
            desc.addClipPreferencesSlaveParam(*param);
            if (page) {
                page->addChild(*param);
            }
            if (group) {
                param->setParent(*group);
            }
        }

        {
            // two secret parameters for Natron's format handling
            int w = 0, h = 0;
            double par = -1.;
            getFormatResolution(eParamFormatPCVideo, &w, &h, &par);
            assert(par != -1);
            {
                Int2DParamDescriptor* param = desc.defineInt2DParam(kParamFormatSize);
                param->setLabel(kParamFormatSizeLabel);
                param->setHint(kParamFormatSizeHint);
                param->setIsSecretAndDisabled(true);
                param->setDefault(w, h);
                if (page) {
                    page->addChild(*param);
                }
                if (group) {
                    param->setParent(*group);
                }
            }

            {
                DoubleParamDescriptor* param = desc.defineDoubleParam(kParamFormatPAR);
                param->setLabel(kParamFormatPARLabel);
                param->setHint(kParamFormatPARHint);
                param->setIsSecretAndDisabled(true);
                param->setRange(0., DBL_MAX);
                param->setDisplayRange(0.5, 2.);
                param->setDefault(par);
                if (page) {
                    page->addChild(*param);
                }
                if (group) {
                    param->setParent(*group);
                }
            }
        }

        {
            BooleanParamDescriptor* param = desc.defineBooleanParam(kParamMouseParams);
            param->setLabel(kParamMouseParamsLabel);
            param->setHint(kParamMouseParamsHint);
            param->setDefault(true);
            param->setAnimates(false);
            if (page) {
                page->addChild(*param);
            }
            if (group) {
                param->setParent(*group);
            }
        }

        {
            RGBAParamDescriptor* param = desc.defineRGBAParam(kParamDate);
            param->setLabel(kParamDateLabel);
            param->setHint(kParamDateHint);
            param->setDefault(1970., 0., 1., 0.);
            param->setRange(0, 0, 1, 0, 9999, 11, 31, 24*60*60);
            param->setDisplayRange(0, 0, 1, 0, 9999, 11, 31, 24*60*60);
            param->setDimensionLabels("y", "m", "d", "s");
            param->setAnimates(true);
            if (page) {
                page->addChild(*param);
            }
            if (group) {
                param->setParent(*group);
            }
        }

        {
            GroupParamDescriptor* sgroup = desc.defineGroupParam(kGroupExtraParameters);
            if (sgroup) {
                sgroup->setLabel(kGroupExtraParametersLabel);
                sgroup->setHint(kGroupExtraParametersHint);
                sgroup->setOpen(false);
                if (page) {
                    page->addChild(*sgroup);
                }
                if (group) {
                    sgroup->setParent(*group);
                }
            }

            {
                IntParamDescriptor* param = desc.defineIntParam(kParamCount);
                param->setLabel(kParamCountLabel);
                param->setHint(kParamCountHint);
                param->setRange(0, nbuniforms);
                param->setDisplayRange(0, nbuniforms);
                param->setAnimates(false);
                if (page) {
                    page->addChild(*param);
                }
                if (sgroup) {
                    param->setParent(*sgroup);
                }
            }

            for (unsigned i = 0; i < nbuniforms; ++i) {
                // generate the number string
                string nb = unsignedToString(i);
                GroupParamDescriptor* pgroup = desc.defineGroupParam(kGroupParameter + nb);
                if (pgroup) {
                    pgroup->setLabel(kGroupParameterLabel + nb);
                    pgroup->setOpen(false);
                    if (page) {
                        page->addChild(*pgroup);
                    }
                    if (sgroup) {
                        pgroup->setParent(*sgroup);
                    }
                }

                {
                    ChoiceParamDescriptor* param = desc.defineChoiceParam(string(kParamType) + nb);
                    param->setLabel(kParamTypeLabel);
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
                    if (pgroup) {
                        param->setParent(*pgroup);
                    }
                }
                {
                    StringParamDescriptor* param = desc.defineStringParam(string(kParamName) + nb);
                    param->setLabel(kParamNameLabel);
                    param->setHint(kParamNameHint);
                    param->setEvaluateOnChange(true);
                    param->setAnimates(false);
                    param->setLayoutHint(eLayoutHintNoNewLine, 1);
                    if (page) {
                        page->addChild(*param);
                    }
                    if (pgroup) {
                        param->setParent(*pgroup);
                    }
                }
                {
                    StringParamDescriptor* param = desc.defineStringParam(string(kParamLabel) + nb);
                    param->setLabel(kParamLabelLabel);
                    param->setHint(kParamLabelHint);
                    param->setEvaluateOnChange(false);
                    param->setAnimates(false);
                    if (page) {
                        page->addChild(*param);
                    }
                    if (pgroup) {
                        param->setParent(*pgroup);
                    }
                }
                {
                    StringParamDescriptor* param = desc.defineStringParam(string(kParamHint) + nb);
                    param->setLabel(kParamHintLabel);
                    param->setHint(kParamHintHint);
                    param->setEvaluateOnChange(false);
                    param->setAnimates(false);
                    if (page) {
                        page->addChild(*param);
                    }
                    if (pgroup) {
                        param->setParent(*pgroup);
                    }
                }
                defineBoolean(desc, nb, page, pgroup);
                defineInt(desc, nb, page, pgroup);
                defineDouble(desc, nb, page, pgroup);
                defineDouble2D(desc, nb, page, pgroup);
                defineDouble3D(desc, nb, page, pgroup);
                defineRGBA(desc, nb, page, pgroup);
            }
        }
    }


#if defined(OFX_SUPPORTS_OPENGLRENDER) && defined(HAVE_OSMESA)
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamEnableGPU);
        param->setLabel(kParamEnableGPULabel);
        param->setHint(kParamEnableGPUHint);
        const ImageEffectHostDescription &gHostDescription = *getImageEffectHostDescription();
        // Resolve advertises OpenGL support in its host description, but never calls render with OpenGL enabled
        if ( gHostDescription.supportsOpenGLRender && (gHostDescription.hostName.compare(0, 14, "DaVinciResolve") != 0) ) {
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
#if defined(HAVE_OSMESA)
    if ( ShadertoyPlugin::OSMesaDriverSelectable() ) {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamCPUDriver);
        param->setLabel(kParamCPUDriverLabel);
        param->setHint(kParamCPUDriverHint);
        assert(param->getNOptions() == ShadertoyPlugin::eCPUDriverSoftPipe);
        param->appendOption(kParamCPUDriverOptionSoftPipe);
        assert(param->getNOptions() == ShadertoyPlugin::eCPUDriverLLVMPipe);
        param->appendOption(kParamCPUDriverOptionLLVMPipe);
        assert(param->getNOptions() == ShadertoyPlugin::eCPUDriverSWR);
        param->appendOption(kParamCPUDriverOptionSWR);
        param->setDefault(kParamCPUDriverDefault);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }

#endif

    {
        PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamRendererInfo);
        param->setLabel(kParamRendererInfoLabel);
        param->setHint(kParamRendererInfoHint);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamHelp);
        param->setLabel(kParamHelpLabel);
        param->setHint(kParamHelpHint);
        if (page) {
            page->addChild(*param);
        }
    }
} // ShadertoyPluginFactory::describeInContext

ImageEffect*
ShadertoyPluginFactory::createInstance(OfxImageEffectHandle handle,
                                       ContextEnum /*context*/)
{
    return new ShadertoyPlugin(handle);
}

static ShadertoyPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

#endif // OFX_SUPPORTS_OPENGLRENDER
