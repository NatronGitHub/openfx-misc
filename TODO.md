### Enhancements to existing nodes

#### Grade

The Normalize button should only compute the white and black points from values where the mask is non-zero, see https://forum.natron.fr/t/how-to-normalize-z-pass-from-blender-3d/1885/7

#### ImageStatistics

Should be maskable, so that it can be used to compute statistics over an image area. Note that when analysing a sequence, the msk may be different at each frame.

#### Shadertoy

- upgrade to Shadertoy 0.9.1:
   - support WebGL 2.0 / OpenGL ES 3.0
     (https://www.khronos.org/registry/OpenGL/specs/es/3.0/GLSL_ES_Specification_3.00.pdf
      and pages 4 and 5 of
      https://www.khronos.org/files/opengles3-quick-reference-card.pdf )
      GLSL 3.30 https://www.khronos.org/registry/OpenGL/specs/gl/GLSLangSpec.3.30.pdf
      Note that this probably means we have to switch to an OpenGL Core profile,
      so the host must give us an OpenGL Core context.
      See also: https://shadertoyunofficial.wordpress.com/2017/02/16/webgl-2-0-vs-webgl-1-0/
- add multipass support (using tabs for UI as in shadertoys)
- synthclipse-compatible comments http://synthclipse.sourceforge.net/user_guide/fragx/commands.html
- use .stoy for the presets shaders, and add the default shadertoy uniforms at the beginning, as in http://synthclipse.sourceforge.net/user_guide/shadertoy.html
- ShaderToy export as in synthclipse http://synthclipse.sourceforge.net/user_guide/shadertoy.html

### Missing Nuke-like nodes

Create OpenFX equivalents to the following Nuke nodes:

- Channel group
  - *Copy*
- Color group
  - *HueShift*
  - *Sampler* (+sampler along a segment)
- Filter group
  - *Defocus*
  - *Glow*
- Merge group
  - *AddMix*

