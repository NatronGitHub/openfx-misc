// https://www.shadertoy.com/view/lly3zd

// iChannel0: Source, filter=linear, wrap=clamp
// BBox: iChannel0

#if 0
void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
        vec2 uv = fragCoord.xy / iResolution.xy;
    uv.y -= 0.5;
    uv.x -= 0.5;
    vec2 uv2 = uv * 2.;

    float doFlip = 0.0;
    if (abs(uv2.x) > 1.0)
    {
        doFlip = mod(floor(uv2.x),2.0);
    }
    else if (uv2.x < 0.0) {
        doFlip = 1.0;
    }

    float doFlipY = 0.0;
    if (abs(uv2.y) > 1.0)
    {
        doFlipY = mod(floor(uv2.y),2.0);
    }
    else if (uv2.y < 0.0) {
        doFlipY = 1.0;
    }

    uv2 = mod(uv2,1.0);
    if (doFlip == 1.0)
        uv2.x = 1.0 - uv2.x;
    if (doFlipY == 1.0)
        uv2.y = 1.0 - uv2.y;

    fragColor = mix(texture(iChannel0, uv2),vec4(0.0,0.0,0.0,0.0), 0.);
}
#else
// The same, but shorter:

void mainImage(out vec4 o, vec2 i)
{
    o = texture(iChannel0, abs(i/iResolution.xy * 2. - 1.));
}
#endif
