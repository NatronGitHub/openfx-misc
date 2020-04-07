// https://www.shadertoy.com/view/XssSDs

// Fast circular blur using a fixed (15) number of samples.

// Adapted to Natron by F. Devernay

// iChannel0: Source, filter=linear, wrap=clamp
// iChannel1: Modulate (Image containing a factor to be applied to the Blur size in the first channel), filter=linear, wrap=clamp
// BBox: iChannel0

const vec2 iRenderScale = vec2(1.,1.);
const vec2 iChannelOffset[4] = vec2[4]( vec2(0.,0.), vec2(0.,0.), vec2(0.,0.), vec2(0.,0.) );
uniform float size = 10.; // Blur Size (Blur size in pixels), min=0., max=20.
uniform bool perpixel_size = false; // Modulate (Modulate the blur size by multiplying it by the first channel of the Modulate input)

vec2 Circle(float Start, float Points, float Point) 
{
	float Rad = (3.141592 * 2.0 * (1.0 / Points)) * (Point + Start);
	return vec2(sin(Rad), cos(Rad));
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec2 uv = fragCoord.xy / iResolution.xy;
    
    float Start = 2.0 / 14.0;
	vec2 Scale = size * iRenderScale / iResolution.xy;
	if (perpixel_size) {
		Scale *= texture2D(iChannel1, (fragCoord.xy-iChannelOffset[1].xy)/iChannelResolution[1].xy).x;
	}
    
    vec4 N0 = texture2D(iChannel0, uv + Circle(Start, 14.0, 0.0) * Scale).rgba;
    vec4 N1 = texture2D(iChannel0, uv + Circle(Start, 14.0, 1.0) * Scale).rgba;
    vec4 N2 = texture2D(iChannel0, uv + Circle(Start, 14.0, 2.0) * Scale).rgba;
    vec4 N3 = texture2D(iChannel0, uv + Circle(Start, 14.0, 3.0) * Scale).rgba;
    vec4 N4 = texture2D(iChannel0, uv + Circle(Start, 14.0, 4.0) * Scale).rgba;
    vec4 N5 = texture2D(iChannel0, uv + Circle(Start, 14.0, 5.0) * Scale).rgba;
    vec4 N6 = texture2D(iChannel0, uv + Circle(Start, 14.0, 6.0) * Scale).rgba;
    vec4 N7 = texture2D(iChannel0, uv + Circle(Start, 14.0, 7.0) * Scale).rgba;
    vec4 N8 = texture2D(iChannel0, uv + Circle(Start, 14.0, 8.0) * Scale).rgba;
    vec4 N9 = texture2D(iChannel0, uv + Circle(Start, 14.0, 9.0) * Scale).rgba;
    vec4 N10 = texture2D(iChannel0, uv + Circle(Start, 14.0, 10.0) * Scale).rgba;
    vec4 N11 = texture2D(iChannel0, uv + Circle(Start, 14.0, 11.0) * Scale).rgba;
    vec4 N12 = texture2D(iChannel0, uv + Circle(Start, 14.0, 12.0) * Scale).rgba;
    vec4 N13 = texture2D(iChannel0, uv + Circle(Start, 14.0, 13.0) * Scale).rgba;
    vec4 N14 = texture2D(iChannel0, uv).rgba;
    
    float W = 1.0 / 15.0;
    
    vec4 color = vec4(0.);
    
	color.rgba =
		(N0 * W) +
		(N1 * W) +
		(N2 * W) +
		(N3 * W) +
		(N4 * W) +
		(N5 * W) +
		(N6 * W) +
		(N7 * W) +
		(N8 * W) +
		(N9 * W) +
		(N10 * W) +
		(N11 * W) +
		(N12 * W) +
		(N13 * W) +
		(N14 * W);
    
    fragColor = color.rgba;
}
