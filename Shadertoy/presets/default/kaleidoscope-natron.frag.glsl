// https://www.shadertoy.com/view/XsfGzj

// iChannel0: Source, filter=linear, wrap=clamp
// BBox: iChannel0

vec2 mirror(vec2 x)
{
        return abs(fract(x/2.0) - 0.5)*2.0;
}

// There are two versions
// One adds up the colors from all the iterations, the other uses only the last.
#if 0
void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
        vec2 uv = -1.0 + 2.0*fragCoord.xy / iResolution.xy;

        float a = iTime*0.2;
        vec4 color = vec4(0.0);
        for (float i = 1.0; i < 10.0; i += 1.0) {
                uv = vec2(sin(a)*uv.y - cos(a)*uv.x, sin(a)*uv.x + cos(a)*uv.y);
                uv = mirror(uv);

                // These two lines can be changed for slightly different effects
                // This is just something simple that looks nice
                a += i;
                a /= i;
                color += texture(iChannel0, mirror(uv*2.0)) * 10.0/i;
        }

        fragColor = color / 28.289;
}
#else
void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
        vec2 uv = -1.0 + 2.0*fragCoord.xy / iResolution.xy;

        float a = iTime*0.2;

        for (float i = 1.0; i < 10.0; i += 1.0) {
                uv = vec2(sin(a)*uv.y - cos(a)*uv.x, sin(a)*uv.x + cos(a)*uv.y);
                uv = mirror(uv);

                // These two lines can be changed for slightly different effects
                // This is just something simple that looks nice
                a += i;
                a /= i;

        }

        fragColor = texture(iChannel0, mirror(uv*2.0));
}
#endif