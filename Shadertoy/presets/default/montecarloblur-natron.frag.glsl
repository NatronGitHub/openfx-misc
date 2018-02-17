// https://www.shadertoy.com/view/MdXXWr

// Monte-Carlo blur.

// Adapted to Natron by F. Devernay

// iChannel0: Source, filter=mipmap, wrap=clamp
// iChannel1: Modulate (Image containing a factor to be applied to the Blur size in the first channel), filter=linear, wrap=clamp
// BBox: iChannel0

const vec2 iRenderScale = vec2(1.,1.);
const vec2 iChannelOffset[4] = vec2[4]( vec2(0.,0.), vec2(0.,0.), vec2(0.,0.), vec2(0.,0.) );
uniform float size = 10.; // Blur Size (Blur size in pixels), min=0., max=20.
uniform int iter = 32; // Samples (Number of samples - higher is better and slower), min=1, max=64
uniform bool perpixel_size = false; // Modulate (Modulate the blur size by multiplying it by the first channel of the Modulate input)

void srand(vec2 a, out float r)
{
	r=sin(iTime+dot(a,vec2(1233.224,1743.335)));
}

float rand(inout float r)
{
	r=fract(3712.65*r+0.61432);
	return (r-0.5)*2.0;
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec2 uv = fragCoord.xy / iResolution.xy;
	vec4 c=vec4(0.0);
	float r;
	srand(uv, r);
	vec2 rv;
	float fSize = size;
	if (perpixel_size) {
		fSize *= texture2D(iChannel1, (fragCoord.xy-iChannelOffset[1].xy)/iChannelResolution[1].xy).x;
	}

	for(int i=0;i<iter;i++)
	{
		rv.x=rand(r);
		rv.y=rand(r);
		c+=texture2D(iChannel0,uv+fSize*rv*iRenderScale.xy/iResolution.xy);
	}
	fragColor = c / float(iter);
}
