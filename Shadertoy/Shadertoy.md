Apply a [Shadertoy](http://www.shadertoy.com) fragment shader (multipass shaders and sound are not supported).

This help only covers the parts of GLSL ES that are relevant for Shadertoy. For the complete specification please have a look at [GLSL ES specification](http://www.khronos.org/registry/gles/specs/2.0/GLSL_ES_Specification_1.0.17.pdf) or pages 3 and 4 of the [OpenGL ES 2.0 quick reference card](https://www.khronos.org/opengles/sdk/docs/reference_cards/OpenGL-ES-2_0-Reference-card.pdf).
See also the [Shadertoy/GLSL tutorial](https://www.shadertoy.com/view/Md23DV).

### Language:

* __Preprocessor:__ `#` `#define` `#undef` `#if` `#ifdef` `#ifndef` `#else` `#elif` `#endif` `#error` `#pragma` `#extension` `#version` `#line`
* __Operators:__ `()` `+` `-` `!` `*` `/` `%` `<` `>` `<=` `>=` `==` `!=` `&&` `||`
* __Comments:__ `//` `/*` `*/`
* __Types:__ void bool int float vec2 vec3 vec4 bvec2 bvec3 bvec4 ivec2 ivec3 ivec4 mat2 mat3 mat4 sampler2D
* __Function Parameter Qualifiers:__ ~~none~~, in, out, inout
* __Global Variable Qualifiers:__ const
* __Vector Components:__ .xyzw .rgba .stpq
* __Flow Control:__ if else for return break continue
* __Output:__ vec4 fragColor
* __Input:__ vec2 fragCoord


### Built-in Functions [(details)](http://www.shaderific.com/glsl-functions/)

#### Angle and Trigonometry Functions

* *type* radians (*type* degrees)
* *type* degrees (*type* radians)
* *type* sin (*type* angle)
* *type* cos (*type* angle)
* *type* tan (*type* angle)
* *type* asin (*type* x)
* *type* acos (*type* x)
* *type* atan (*type* y, *type* x)
* *type* atan (*type* yoverx)

### Exponential Functions

* *type* pow (*type* x, *type* y)
* *type* exp (*type* x)
* *type* log (*type* x)
* *type* exp2 (*type* x)
* *type* log2 (*type* x)
* *type* sqrt (*type* x)
* *type* inversesqrt (*type* x)

#### Common Functions

* *type* abs (*type* x)
* *type* sign (*type* x)
* *type* floor (*type* x)
* *type* ceil (*type* x)
* *type* fract (*type* x)
* *type* mod (*type* x, float y)
* *type* mod (*type* x, *type* y)
* *type* min (*type* x, *type* y)
* *type* min (*type* x, float y)
* *type* max (*type* x, *type* y)
* *type* max (*type* x, float y)
* *type* clamp (*type* x, *type* minV, *type* maxV)
* *type* clamp (*type* x, float minV, float maxV)
* *type* mix (*type* x, *type* y, *type* a)
* *type* mix (*type* x, *type* y, float a)
* *type* step (*type* edge, *type* x)
* *type* step (float edge, *type* x)
* *type* smoothstep (*type* a, *type* b, *type* x)
* *type* smoothstep (float a, float b, *type* x)

#### Geometric Functions

* float length (*type* x)
* float distance (*type* p0, *type* p1)
* float dot (*type* x, *type* y)
* vec3 cross (vec3 x, vec3 y)
* *type* normalize (*type* x)
* *type* faceforward (*type* N, *type* I, *type* Nref)
* *type* reflect (*type* I, *type* N)
* *type* refract (*type* I, *type* N,float eta)

#### Matrix Functions

* mat matrixCompMult (mat x, mat y)

#### Vector Relational Functions

* bvec lessThan(vec x, vec y)
* bvec lessThan(ivec x, ivec y)
* bvec lessThanEqual(vec x, vec y)
* bvec lessThanEqual(ivec x, ivec y)
* bvec greaterThan(vec x, vec y)
* bvec greaterThan(ivec x, ivec y)
* bvec greaterThanEqual(vec x, vec y)
* bvec greaterThanEqual(ivec x, ivec y)
* bvec equal(vec x, vec y)
* bvec equal(ivec x, ivec y)
* bvec equal(bvec x, bvec y)
* bvec notEqual(vec x, vec y)
* bvec notEqual(ivec x, ivec y)
* bvec notEqual(bvec x, bvec y)
* bool any(bvec x)
* bool all(bvec x)
* bvec not(bvec x)

#### Texture Lookup Functions

* vec4 texture2D(sampler2D sampler, vec2 coord )
* vec4 texture2D(sampler2D sampler, vec2 coord, float bias)
* vec4 textureCube(samplerCube sampler, vec3 coord)
* vec4 texture2DProj(sampler2D sampler, vec3 coord )
* vec4 texture2DProj(sampler2D sampler, vec3 coord, float bias)
* vec4 texture2DProj(sampler2D sampler, vec4 coord)
* vec4 texture2DProj(sampler2D sampler, vec4 coord, float bias)
* vec4 texture2DLodEXT(sampler2D sampler, vec2 coord, float lod)
* vec4 texture2DProjLodEXT(sampler2D sampler, vec3 coord, float lod)
* vec4 texture2DProjLodEXT(sampler2D sampler, vec4 coord, float lod)
* vec4 textureCubeLodEXT(samplerCube sampler, vec3 coord, float lod)
* vec4 texture2DGradEXT(sampler2D sampler, vec2 P, vec2 dPdx, vec2 dPdy)
* vec4 texture2DProjGradEXT(sampler2D sampler, vec3 P, vec2 dPdx, vec2 dPdy)
* vec4 texture2DProjGradEXT(sampler2D sampler, vec4 P, vec2 dPdx, vec2 dPdy)
* vec4 textureCubeGradEXT(samplerCube sampler, vec3 P, vec3 dPdx, vec3 dPdy)

#### Function Derivatives

* *type* dFdx( *type* x ), dFdy( *type* x )
* *type* fwidth( *type* p )


### How-to

* __Use structs:__ `struct myDataType { float occlusion; vec3 color; }; myDataType myData = myDataType(0.7, vec3(1.0, 2.0, 3.0));`
* __Initialize arrays:__ arrays cannot be initialized in WebGL.
* __Do conversions:__ `int a = 3; float b = float(a);`
* __Do component swizzling:__ `vec4 a = vec4(1.0,2.0,3.0,4.0); vec4 b = a.zyyw;`
* __Access matrix components:__ `mat4 m; m[1] = vec4(2.0); m[0][0] = 1.0; m[2][3] = 2.0;`


### Be careful!

* __the f suffix for floating point numbers:__ 1.0f is illegal in GLSL. You must use 1.0
* __saturate():__ saturate(x) doesn't exist in GLSL. Use clamp(x,0.0,1.0) instead
* __pow/sqrt:__ please don't feed sqrt() and pow() with negative numbers. Add an abs() or max(0.0,) to the argument
* __mod:__ please don't do mod(x,0.0). This is undefined in some platforms
* __variables:__ initialize your variables! Don't assume they'll be set to zero by default
* __functions:__ don't call your functions the same as some of your variables


### Shadertoy Inputs

Type | Name | Function | Description
--- | --- | --- | ---
vec3 | iResolution | image | The viewport resolution (z is pixel aspect ratio, usually 1.0)
float | iGlobalTime | image/sound | Current time in seconds
float | iTimeDelta | image | Time it takes to render a frame, in seconds
int | iFrame | image | Current frame
float | iFrameRate | image | Number of frames rendered per second
float | iChannelTime["STRINGISE(NBINPUTS)"] | image | Time for channel (if video or sound), in seconds
vec3 | iChannelResolution["STRINGISE(NBINPUTS)"] | image/sound | Input texture resolution for each channel
vec4 | iMouse | image | xy = current pixel coords (if LMB is down). zw = click pixel
sampler2D | iChannel{i} | image/sound | Sampler for input textures i
vec4 | iDate | image/sound | Year, month, day, time in seconds in .xyzw
float | iSampleRate | image/sound | The sound sample rate (typically 44100)
vec2 | iRenderScale | image | The OpenFX render scale (e.g. 0.5,0.5 when rendering half-size) [OFX plugin only]


### Shadertoy Outputs
For image shaders, fragColor is used as output channel. It is not, for now, mandatory but recommended to leave the alpha channel to 1.0.

For sound shaders, the mainSound() function returns a vec2 containing the left and right (stereo) sound channel wave data.


### OpenFX extensions to Shadertoy

* The pre-defined `iRenderScale` uniform contains the current render scale. Basically all pixel sizes must be multiplied by the renderscale to get a scale-independent effect. For compatibility with Shadertoy, the first line that starts with `const vec2 iRenderScale` is ignored (the full line should be `const vec2 iRenderScale = vec2(1.,1.);`).
* The shader may define additional uniforms, which should have a default value, as in `uniform vec2 blurSize = vec2(5., 5.);`.
  These uniforms can be made available as OpenFX parameters using settings in the 'Extra parameters' group, which can be set automatically using the 'Auto. Params' button (in this case, parameters are updated when the image is rendered).
  A parameter label and help string can be given in the comment on the same line. The help string must be in parenthesis.
  `uniform vec2 blurSize = vec2(5., 5.); // Blur Size (The blur size in pixels.)`
  min/max values can also be given after a comma. The strings must be exactly `min=` and `max=`, without additional spaces, separated by a comma, and the values must have the same dimension as the uniform:
  `uniform vec2 blurSize = vec2(5., 5.); // Blur Size (The blur size in pixels.), min=(0.,0.), max=(1000.,1000.)`
* The following comment line placed in the shader gives a label and help string to input 1 (the comment must be the only thing on the line):
  `// iChannel1: Noise (A noise texture to be used for random number calculations. The texture should not be frame-varying.)`
* This one also sets the filter and wrap parameters:
  `// iChannel0: Source (Source image.), filter=linear, wrap=clamp`
* And this one sets the output bouding box (possible values are Default, Union, Intersection, and iChannel0 to iChannel3):
  `// BBox: iChannel0`

### Default textures and videos

The default shadertoy textures and videos are avalaible from the [Shadertoy](http://www.shadertoy.com) web site. In order to mimic the behavior of each shader, download the corresponding textures or videos and connect them to the proper input.

- Textures: [tex00](https://www.shadertoy.com/presets/tex00.jpg),  [tex01](https://www.shadertoy.com/presets/tex01.jpg),  [tex02](https://www.shadertoy.com/presets/tex02.jpg),  [tex03](https://www.shadertoy.com/presets/tex03.jpg),  [tex04](https://www.shadertoy.com/presets/tex04.jpg),  [tex05](https://www.shadertoy.com/presets/tex05.jpg),  [tex06](https://www.shadertoy.com/presets/tex06.jpg),  [tex07](https://www.shadertoy.com/presets/tex07.jpg),  [tex08](https://www.shadertoy.com/presets/tex08.jpg),  [tex09](https://www.shadertoy.com/presets/tex09.jpg),  [tex10](https://www.shadertoy.com/presets/tex10.png),  [tex11](https://www.shadertoy.com/presets/tex11.png),  [tex12](https://www.shadertoy.com/presets/tex12.png),  [tex14](https://www.shadertoy.com/presets/tex14.png),  [tex15](https://www.shadertoy.com/presets/tex15.png),  [tex16](https://www.shadertoy.com/presets/tex16.png),  [tex17](https://www.shadertoy.com/presets/tex17.jpg),  [tex18](https://www.shadertoy.com/presets/tex18.jpg),  [tex19](https://www.shadertoy.com/presets/tex19.png),  [tex20](https://www.shadertoy.com/presets/tex20.jpg),  [tex21](https://www.shadertoy.com/presets/tex21.png).
- Videos: [vid00](https://www.shadertoy.com/presets/vid00.ogv),  [vid01](https://www.shadertoy.com/presets/vid01.webm),  [vid02](https://www.shadertoy.com/presets/vid02.ogv),  [vid03](https://www.shadertoy.com/presets/vid03.webm).
- Cubemaps: [cube00_0](https://www.shadertoy.com/presets/cube00_0.jpg),  [cube01_0](https://www.shadertoy.com/presets/cube01_0.png),  [cube02_0](https://www.shadertoy.com/presets/cube02_0.jpg),  [cube03_0](https://www.shadertoy.com/presets/cube03_0.png),  [cube04_0](https://www.shadertoy.com/presets/cube04_0.png),  [cube05](https://www.shadertoy.com/presets/cube05_0.png)
