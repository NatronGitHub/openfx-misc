// from: https://www.shadertoy.com/view/ltB3zw

// Three colored sine-bands interweaving

vec3 calcSine(vec2 uv, 
              float frequency, float amplitude, float shift, float offset,
              vec3 color, float width)
{
    float y = sin(iTime * frequency + shift + uv.x) * amplitude + offset;
    float scale = smoothstep(width, 0.0, distance(y, uv.y));
    return color * scale;
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec2 uv = fragCoord.xy / iResolution.xy;
    vec3 color = vec3(0.0);
    
    color += calcSine(uv, 2.0, 0.25, 0.0, 0.5, vec3(0.0, 0.0, 1.0), 0.3);
    color += calcSine(uv, 2.6, 0.25, 0.2, 0.5, vec3(0.0, 1.0, 0.0), 0.3);
    color += calcSine(uv, 2.9, 0.25, 0.4, 0.5, vec3(1.0, 0.0, 0.0), 0.3);
    
	fragColor = vec4(color,1.0);
}
