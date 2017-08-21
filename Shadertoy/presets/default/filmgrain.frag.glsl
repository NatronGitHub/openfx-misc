// https://www.shadertoy.com/view/4sXSWs

// Simple filter

// iChannel0: Source, filter=linear, wrap=clamp
// BBox: iChannel0

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec2 uv = fragCoord.xy / iResolution.xy;
    
    vec4 color = texture2D(iChannel0, uv);
    
    float strength = 16.0;
    
    float x = (uv.x + 4.0 ) * (uv.y + 4.0 ) * (iTime * 10.0);
	vec4 grain = vec4(mod((mod(x, 13.0) + 1.0) * (mod(x, 123.0) + 1.0), 0.01)-0.005) * strength;
    
    if(abs(uv.x - 0.5) < 0.002)
        color = vec4(0.0);
    
    if(uv.x > 0.5)
    {
    	grain = 1.0 - grain;
		fragColor = color * grain;
    }
    else
    {
		fragColor = color + grain;
    }
}
