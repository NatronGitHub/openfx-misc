// https://www.shadertoy.com/view/lly3zd

// iChannel0: Source, filter=linear, wrap=clamp
// BBox: iChannel0

uniform float interval = 6.; Interval, min=0., max=100.
void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 uv = fragCoord.xy / iResolution.xy;
    vec4 color =  texture(iChannel0, uv);
    float a = step(mod(fragCoord.x + fragCoord.y, interval) / (interval - 1.0), 0.5);
        fragColor = vec4(color.rgb * a,1.0);
}
