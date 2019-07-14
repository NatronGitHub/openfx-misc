Apply a [Shadertoy](http://www.shadertoy.com) fragment shader (multipass shaders and sound are not supported).

This plugin implements [Shadertoy 0.8.8](https://www.shadertoy.com/changelog), but multipass shaders and sound are not supported. Some multipass shaders can still be implemented by chaining several Shadertoy nodes, one for each pass.

[Shadertoy 0.8.8](https://www.shadertoy.com/changelog) uses WebGL 1.0 (a.k.a. [GLSL ES 1.0](https://www.khronos.org/registry/OpenGL/specs/es/2.0/GLSL_ES_Specification_1.00.pdf) from GLES 2.0), based on [GLSL 1.20](https://www.khronos.org/registry/OpenGL/specs/gl/GLSLangSpec.1.20.pdf).

Note that the more recent [Shadertoy 0.9.1](https://www.shadertoy.com/changelog) uses WebGL 2.0 (a.k.a. [GLSL ES 3.0](https://www.khronos.org/registry/OpenGL/specs/es/3.0/GLSL_ES_Specification_3.00.pdf) from GLES 3.0), based on [GLSL 3.3](https://www.khronos.org/registry/OpenGL/specs/gl/GLSLangSpec.3.30.pdf).

This help only covers the parts of GLSL ES that are relevant for Shadertoy. For the complete specification please have a look at [GLSL ES 1.0 specification](https://www.khronos.org/registry/OpenGL/specs/es/2.0/GLSL_ES_Specification_1.00.pdf) or pages 3 and 4 of the [OpenGL ES 2.0 quick reference card](https://www.khronos.org/opengles/sdk/docs/reference_cards/OpenGL-ES-2_0-Reference-card.pdf).
See also the [Shadertoy/GLSL tutorial](https://www.shadertoy.com/view/Md23DV).

### Image shaders

Image shaders implement the `mainImage()` function in order to generate the procedural images by computing a color for each pixel. This function is expected to be called once per pixel, and it is responsibility of the host application to provide the right inputs to it and get the output color from it and assign it to the screen pixel. The prototype is:

`void mainImage( out vec4 fragColor, in vec2 fragCoord );`

where `fragCoord` contains the pixel coordinates for which the shader needs to compute a color. The coordinates are in pixel units, ranging from 0.5 to resolution-0.5, over the rendering surface, where the resolution is passed to the shader through the `iResolution` uniform (see below).

The resulting color is gathered in `fragColor` as a four component vector.

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
float | iTime | image/sound | Current time in seconds
float | iTimeDelta | image | Time it takes to render a frame, in seconds
int | iFrame | image | Current frame
float | iFrameRate | image | Number of frames rendered per second
float | iChannelTime["STRINGISE(NBINPUTS)"] | image | Time for channel (if video or sound), in seconds
vec3 | iChannelResolution["STRINGISE(NBINPUTS)"] | image/sound | Input texture resolution for each channel
vec2 | iChannelOffset["STRINGISE(NBINPUTS)"] | image | Input texture offset in pixel coords for each channel
vec4 | iMouse | image | xy = current pixel coords (if LMB is down). zw = click pixel
sampler2D | iChannel{i} | image/sound | Sampler for input textures i
vec4 | iDate | image/sound | Year, month, day, time in seconds in .xyzw
float | iSampleRate | image/sound | The sound sample rate (typically 44100)
vec2 | iRenderScale | image | The OpenFX render scale (e.g. 0.5,0.5 when rendering half-size) [OFX plugin only]


### Shadertoy Outputs
For image shaders, fragColor is used as output channel. It is not, for now, mandatory but recommended to leave the alpha channel to 1.0.

For sound shaders, the mainSound() function returns a vec2 containing the left and right (stereo) sound channel wave data.


### OpenFX extensions to Shadertoy

Shadertoy was extended to:

* Expose shader parameters as uniforms, which are presented as OpenFX parameters.
* Provide the description and help for these parameters directly in the GLSL code.
* Add a default uniform containing the render scale. In OpenFX, a render scale of 1 means that the image is rendered at full resolution, 0.5 at half resolution, etc. This can be used to scale parameter values so that the final aspect does not depend on the render scale. For example, a blur size parameter given in pixels at full resolution would have to be multiplied by the render scale.
* Add a default uniform containing the offset of the processed texture with respect to the position of the origin.

The extensions are:

* The pre-defined `iRenderScale` uniform contains the current render scale. Basically all pixel sizes must be multiplied by the renderscale to get a scale-independent effect. For compatibility with Shadertoy, the first line that starts with `const vec2 iRenderScale` is ignored (the full line should be `const vec2 iRenderScale = vec2(1.,1.);`).
* The pre-defined `iChannelOffset` uniform contains the texture offset for each channel relative to channel 0. For compatibility with Shadertoy, the first line that starts with `const vec2 iChannelOffset` is ignored (the full line should be `const vec2 iChannelOffset[4] = vec2[4]( vec2(0.,0.), vec2(0.,0.), vec2(0.,0.), vec2(0.,0.) );`).
* The shader may define additional uniforms, which should have a default value, as in `uniform vec2 blurSize = vec2(5., 5.);`.
  These uniforms can be made available as OpenFX parameters using settings in the 'Extra parameters' group, which can be set automatically using the 'Auto. Params' button (automatic parameters are only updated if the node is connected to a Viewer). Supported uniform types are: `bool`, `int`, `float`, `vec2`, `vec3`, `vec4`.
  A parameter label and help string can be given in the comment on the same line. The help string must be in parenthesis.
  `uniform vec2 blurSize = vec2(5., 5.); // Blur Size (The blur size in pixels.)`
  min/max values can also be given after a comma. The strings must be exactly `min=` and `max=`, without additional spaces, separated by a comma, and the values must have the same dimension as the uniform:
  `uniform vec2 blurSize = vec2(5., 5.); // Blur Size (The blur size in pixels.), min=(0.,0.), max=(1000.,1000.)`
* The following comment line placed in the shader gives a label and help string to input 1 (the comment must be the only thing on the line):
  `// iChannel1: Noise (A noise texture to be used for random number calculations. The texture should not be frame-varying.)`
* This one also sets the filter and wrap parameters:
  `// iChannel0: Source (Source image.), filter=linear, wrap=clamp`
* And this one sets the output bounding box (possible values are Default, Union, Intersection, and iChannel0 to iChannel3):
  `// BBox: iChannel0`

### Converting a Shadertoy for use in OpenFX

To better understand how to modify a Shadertoy for OpenFX, let use take the simple [Gaussian blur](https://www.shadertoy.com/view/XdfGDH) example, which is also available as a preset in the Shadertoy node.

In Natron, create a new project, create a Shadertoy node, connect the input 1 of the Viewer to the output of the Shadertoy node. This should give you a blurry color image that corresponds to the default Shadertoy source code. The Shadertoy node should have four inputs, named "iChannel0" to "iChannel3".

In the Shadertoy node parameters, open the "Image Shader" group. You should see the GLSL source code. Now in the "Load from Preset" choice, select "Blur/Gaussian Blur". The viewer should display a black image, but you should also notice that the Shadertoy node now has two visible inputs: "Source" and "Modulate" (in Nuke, these inputs are still called iChannel0 and iChannel1). Create a Read node that reads a still image or a video, and connect it to the "Source" input. A blurred version of the image should now appear in the viewer. You should also notice that two parameters appeared at the top of the parameters for the Shadertoy node: "Size" and "Modulate". Play with the "Size" parameter and see how it affects the blur size (you may have to zoom on the image to see precisely the effect).

Now let us examine the modifications that were brought to the [original GLSL code](https://www.shadertoy.com/view/XdfGDH):

These three comment lines describe the label, filter, and wrap parameters for each input, as well as the size of the output bounding box (also called "region of definition"):

    // iChannel0: Source, filter=linear, wrap=clamp
    // iChannel1: Modulate (Image containing a factor to be applied to the Blur size in the first channel), filter=linear, wrap=clamp
    // BBox: iChannel0


Two constant global variables were added, which are ignored by the Shadertoy plugin, so that you can still copy-and-paste the source code in Shadertoy 0.8.8 and it still works (unfortunately, it does not work anymore with later versions of Shadertoy). You can safely ignore these:

    const vec2 iRenderScale = vec2(1.,1.);
    const vec2 iChannelOffset[4] = vec2[4]( vec2(0.,0.), vec2(0.,0.), vec2(0.,0.), vec2(0.,0.) );


Then the uniform section gives the list of what will appear as OpenFX parameters, together with their default value, label, help string, and default range. Note that in the original Shadertoy code, the blur size was a constant hidden inside the code. Finding out the parameters of a Shadertoy requires precise code inspection. If you modify this part of the code, pressing the "Auto. Params" button will apply these changes to the OpenFX parameters:

    uniform float size = 10.; // Size (Size of the filter kernel in pixel units. The standard deviation of the corresponding Gaussian is size/2.4.), min=0., max=21.
    uniform bool perpixel_size = false; // Modulate (Modulate the blur size by multiplying it by the first channel of the Modulate input)


In the `mainImage` function, which does the processing, we compute the `mSize` and `kSize` variables, which are the kernel size and mask size for that particular algorithm, from the "Size" parameter, multiplied by the render scale to get a scale-invariant effect. If the "Modulate" check box is on, we also multiply the size by the value found in the first channel (which is red, not alpha) of the "Modulate" input, which is in the iChannel1 texture according to the comments at the beginning of the source code. This can be use to modulate the blur size depending on the position in the image. The "Modulate" input may be for example connected to the output of a Roto node (with the "R" checkbox checked in the Roto node). Since the Roto output may not have the same size and origin as the Source image, we take care of these by using the iChannelOffset and iChannelResolution values for input 1.

    float fSize = size * iRenderScale.x;
    if (perpixel_size) {
      fSize *= texture2D(iChannel1, (fragCoord.xy-iChannelOffset[1].xy)/iChannelResolution[1].xy).x;
    }
    int kSize = int(min(int((fSize-1)/2), KSIZE_MAX));
    int mSize = kSize*2+1;

In the rest of the code, the only difference is that the blur size is not constant and equal to 7, but comes from the fSize variable:

    float sigma = fSize / 2.4;

### Issues with Gamma correction

OpenGL processing supposes all textures are linear, i.e. not gamma-compressed. This for example about bilinear interpolation on textures: this only works if the intensities are represented linearly. So a proper OpenGL rendering pipe should in principle:

1. Convert all textures to a linear representation (many 8-bit textures are gamma-compressed)
2. Render with OpenGL
3. Gamma-compress the linear framebuffer for display

When processing floating-point buffers in OpenFX, the color representation is usually linear, which means that the OpenFX host usually performs steps 1 and 3 anyway (that includes Natron and Nuke): the images given to an OpenFX plugins are in linear color space, and their output is also supposed to be linear.

However, many OpenGL applications, including Shadertoy and most games, skip steps 1 and 3 (mainly for performance issue): they process gamma-compressed textures as if they were linear, and sometimes have to boost their output by gamma compression so that it looks nice on a standard display (which usually accepts a sRGB-compressed framebuffer).

This is why many shaders from Shadertoy convert their output from linear to sRGB or gamma=2.2, see for example the `srgb2lin` and `lin2srgb` functions in https://www.shadertoy.com/view/XsfXzf . These conversions *must* be removed when using the shader in OpenFX.

An alternative solution would be to convert all Shadertoy inputs from linear to sRGB, and convert back all outputs to linear, either inside the Shadertoy node, or using external conversion nodes (such as OCIOColorSpace). But this is a bad option, because this adds useless processing. Removing the srgb2lin and lin2srgb conversions from the shader source is a much better option (these functions may have different names, or there may simply be operations line `pow(c,vec3(2.2))` and/or `pow(c,vec3(1./2.2))` in the GLSL code).

As an example, take a look at the changes made to the [Barrel Blur Chroma](https://www.shadertoy.com/view/XssGz8) Shadertoy: the OpenFX version is available as a preset in the Shadertoy node as "Effects/Barrel Blur Chroma". When it was converted to OpenFX, all gamma compression and decompression operations were identified and removed.

### Multipass shaders

Most multipass shaders (those using BufA, BufB, BufC, or BufD) can be implemented using the Shadertoy plugin.

The shader sources for two sample multipass shadertoys are available as Natron PyPlugs (but the shader sources are also available separately next to the PyPlugs if you want to use these in another OpenFX host:

- a [3-pass circular bokeh blur](https://www.shadertoy.com/view/Xd33Dl) (available as [Community/GLSL/BokehCircular_GL](https://github.com/NatronGitHub/natron-plugins/tree/master/GLSL/Blur/BokehCircular_GL) in natron-plugins)
- a [4-pass octagonal bokeh blur](https://www.shadertoy.com/view/lst3Df) (available as [Community/GLSL/BokehOctagon_GL](https://github.com/NatronGitHub/natron-plugins/tree/master/GLSL/Blur/BokehOctagon_GL) in natron-plugins)

The principle is very simple: since multipass cannot be done using a single Shadertoy, use several Shadertoy nodes, route the textures between them, and link the parameters. You can learn from these two examples. To figure out the route between textures, click on the tab for each shader in shadertoy.com, and check which shader output is connected to the input textures (iChannel0, etc.) for this shader. The connections between nodes should follow these rules.

The only multipass effects that can not be implemented are the shaders that read back the content of a buffer to compute that same buffer, because compositing graphs cannot have loops (the execution of such a graph would cause an infinite recursion). One example is [this progressive lightmap render](https://www.shadertoy.com/view/MttSWS), where BufB from the previous render is read back as iChannel1 in the BufB shader.

### Default textures and videos

The default shadertoy textures and videos are available from the [Shadertoy](http://www.shadertoy.com) web site. In order to mimic the behavior of each shader, download the corresponding textures or videos and connect them to the proper input.

- Textures: [tex00](https://www.shadertoy.com/presets/tex00.jpg),  [tex01](https://www.shadertoy.com/presets/tex01.jpg),  [tex02](https://www.shadertoy.com/presets/tex02.jpg),  [tex03](https://www.shadertoy.com/presets/tex03.jpg),  [tex04](https://www.shadertoy.com/presets/tex04.jpg),  [tex05](https://www.shadertoy.com/presets/tex05.jpg),  [tex06](https://www.shadertoy.com/presets/tex06.jpg),  [tex07](https://www.shadertoy.com/presets/tex07.jpg),  [tex08](https://www.shadertoy.com/presets/tex08.jpg),  [tex09](https://www.shadertoy.com/presets/tex09.jpg),  [tex10](https://www.shadertoy.com/presets/tex10.png),  [tex11](https://www.shadertoy.com/presets/tex11.png),  [tex12](https://www.shadertoy.com/presets/tex12.png),  [tex14](https://www.shadertoy.com/presets/tex14.png),  [tex15](https://www.shadertoy.com/presets/tex15.png),  [tex16](https://www.shadertoy.com/presets/tex16.png),  [tex17](https://www.shadertoy.com/presets/tex17.jpg),  [tex18](https://www.shadertoy.com/presets/tex18.jpg),  [tex19](https://www.shadertoy.com/presets/tex19.png),  [tex20](https://www.shadertoy.com/presets/tex20.jpg),  [tex21](https://www.shadertoy.com/presets/tex21.png).
- Videos: [vid00](https://www.shadertoy.com/presets/vid00.ogv),  [vid01](https://www.shadertoy.com/presets/vid01.webm),  [vid02](https://www.shadertoy.com/presets/vid02.ogv),  [vid03](https://www.shadertoy.com/presets/vid03.webm).
- Cubemaps: [cube00_0](https://www.shadertoy.com/presets/cube00_0.jpg),  [cube01_0](https://www.shadertoy.com/presets/cube01_0.png),  [cube02_0](https://www.shadertoy.com/presets/cube02_0.jpg),  [cube03_0](https://www.shadertoy.com/presets/cube03_0.png),  [cube04_0](https://www.shadertoy.com/presets/cube04_0.png),  [cube05](https://www.shadertoy.com/presets/cube05_0.png)
