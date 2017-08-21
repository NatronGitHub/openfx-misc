// Very simple blur using mipmapping

// Adapted to Natron by F. Devernay

// iChannel0: Source, filter=mipmap, wrap=clamp
// iChannel1: Modulate (Image containing a factor to be applied to the Blur size in the first channel), filter=linear, wrap=clamp
// BBox: iChannel0

const vec2 iRenderScale = vec2(1.,1.);
const vec2 iChannelOffset[4] = vec2[4]( vec2(0.,0.), vec2(0.,0.), vec2(0.,0.), vec2(0.,0.) );
uniform float size = 10.; // Blur Size (Blur size in pixels), min=0., max=64.
uniform bool perpixel_size = false; // Modulate (Modulate the blur size by multiplying it by the first channel of the Modulate input)

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec2 uv = fragCoord.xy/iResolution.xy;
	float s = iRenderScale.x * size;
	if (perpixel_size) {
		s *= texture2D(iChannel1, (fragCoord.xy-iChannelOffset[1].xy)/iChannelResolution[1].xy).x;
	}
	fragColor = texture2D(iChannel0, uv, log2(s));
}
