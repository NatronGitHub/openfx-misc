// https://www.shadertoy.com/view/4d2Xzw
// Bokeh disc.
// original fast version by David Hoskins.
// License Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License.

// Fixed and adapted to Natron by F. Devernay:
// - avoid numerical divergence (length of vangle growing)
// - use mipmaps to limit noise when the spacing between samples is too large.

// iChannel0: Source, filter=mipmap, wrap=clamp
// iChannel1: Modulate (Image containing a factor to be applied to the Blur size in the first channel), filter=linear, wrap=clamp
// BBox: iChannel0

// The Golden Angle is (3.-sqrt(5.0))*PI radians, which doesn't precompiled for some reason.
// The compiler is a dunce I tells-ya!!
#define GOLDEN_ANGLE 2.39996

const vec2 iRenderScale = vec2(1.,1.);
const vec2 iChannelOffset[4] = vec2[4]( vec2(0.,0.), vec2(0.,0.), vec2(0.,0.), vec2(0.,0.) );
uniform float size = 10.; // Blur Size (Blur size in pixels), min=0., max=200.
uniform int ITERATIONS = 150; // Samples (Number of samples - higher is better and slower), min=2, max=1024
uniform bool perpixel_size = false; // Modulate (Modulate the blur size by multiplying it by the first channel of the Modulate input)

mat2 rot = mat2(cos(GOLDEN_ANGLE), sin(GOLDEN_ANGLE), -sin(GOLDEN_ANGLE), cos(GOLDEN_ANGLE));
float sqrtiter = sqrt(float(ITERATIONS));

// The original fast version had the following issue:
// - the length of the vangle vector changes, although it should remain the same -> renormalize
// - the radius was sqrt(2) too large => properly compute the base norm
//-------------------------------------------------------------------------------------------
vec4 Bokeh(sampler2D tex, vec2 uv, float radius)
{
    // the spacing is the square root of the density on average: sqrt(pi*r^2/n)
    float spacing = sqrt(3.141592652) * radius * iRenderScale.x / sqrtiter;
    float level = log2(spacing);

    vec4 acc = vec4(0);
    float r = 1.;
    vec2 vangle = vec2(0.,1.);
    float vanglenorm = radius / sqrtiter / sqrt(2);
    vec2 uvscale = iRenderScale.xy / iResolution.xy;
    for (int j = 0; j < ITERATIONS; j++) {  
        // the approx increase in the scale of sqrt(0, 1, 2, 3...)
        r += 1. / r;
	    // r = 1. + sqrt(j) * sqrt(2); // slow version - gives almost the same result
        vangle = rot * vangle;
	    vangle /= length(vangle);
	    // vangle = vec2(cos(GOLDEN_ANGLE*j), sin(GOLDEN_ANGLE*j)); // slow version
        vec4 col = texture2D(tex, uv + (r-1.) * vangle * vanglenorm * uvscale, level).rgba; /// ... Sample the image
        acc += col;
    }
    return acc / ITERATIONS;
}

#if 0
// reference implementation
//-------------------------------------------------------------------------------------------
vec4 Bokeh2(sampler2D tex, vec2 uv, float radius)
{
    // the spacing is the square root of the density on average: sqrt(pi*r^2/n)
    float spacing = sqrt(3.141592652/ITERATIONS) * radius * iRenderScale.x;
    float level = log2(spacing);

    vec4 acc = vec4(0);

    // Vogel's method, described at http://blog.marmakoide.org/?p=1
    for (int j = 0; j < ITERATIONS; j++) {  
        float theta = GOLDEN_ANGLE * j;
        float r = sqrt(j) / sqrtiter;
        vec2 p = r * vec2(cos(theta), sin(theta));
        vec4 col = texture2D(tex, uv + radius * p * iRenderScale.xy / iResolution.xy, level).rgba; /// ... Sample the image
        acc += col;
    }
    return acc / ITERATIONS;
}
#endif

//-------------------------------------------------------------------------------------------
void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 uv = fragCoord.xy / iResolution.xy;
    
    float rad = size;
    if (perpixel_size) {
        rad *= texture2D(iChannel1, (fragCoord.xy-iChannelOffset[1].xy)/iChannelResolution[1].xy).x;
    }
    
    fragColor = Bokeh(iChannel0, uv, rad);
}
