// https://www.shadertoy.com/view/XsfSDs

// Simple filter.
// Update: Now with mouse support

// iChannel0: Source, filter=mipmap, wrap=repeat
// BBox: iChannel0

const int nsamples = 10;

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 center = iMouse.xy /iResolution.xy;
	float blurStart = 1.0;
    float blurWidth = 0.1;

    
	vec2 uv = fragCoord.xy / iResolution.xy;
    
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
