// https://www.shadertoy.com/view/XsfSDs

// Simple filter.
// Update: Now with mouse support

// iChannel0: Source, filter=linear, wrap=clamp
// iChannel1: Modulate (Image containing a factor to be applied to the Blur size in the first channel), filter=linear, wrap=clamp
// BBox: iChannel0

const vec2 iRenderScale = vec2(1.,1.);
const vec2 iChannelOffset[4] = vec2[4]( vec2(0.,0.), vec2(0.,0.), vec2(0.,0.), vec2(0.,0.) );
uniform float amplitude = 0.1; // Amplitude (Zoom amplitude), min=-1., max=2.
uniform bool perpixel_size = false; // Modulate (Modulate the amplitude by multiplying it by the first channel of the Modulate input)
uniform int nsamples = 10; // Samples (Number of samples - higher is better and slower), min=2, max=64

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 center = iMouse.xy /iResolution.xy;
	const float blurStart = 1.0;

    
	vec2 uv = fragCoord.xy / iResolution.xy;
    
    float blurWidth = amplitude;
    	if (perpixel_size) {
		blurWidth *= texture2D(iChannel1, (fragCoord.xy-iChannelOffset[1].xy)/iChannelResolution[1].xy).x;
	}
    uv -= center;

    float precompute = blurWidth * (1.0 / float(nsamples - 1));
    
    vec4 color = vec4(0.0);
    for(int i = 0; i < nsamples; i++)
    {
        float scale = blurStart + (float(i)* precompute);
        color += texture2D(iChannel0, uv * scale + center);
    }
    
    
    color /= float(nsamples);
    
	fragColor = color;
}
