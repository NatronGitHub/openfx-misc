// https://www.shadertoy.com/view/XsfGzj

// iChannel0: Source, filter=linear, wrap=clamp
// BBox: iChannel0

// Adapted to Natron by F. Devernay

uniform float size = 1.; // Size, min=0.0, max=10.
uniform bool addup = false; // Add Up
uniform float speed = 0.2; // Speed, min=0., max=10.

vec2 mirror(vec2 x)
{
        return abs(fract(x/2.0) - 0.5)*2.0;
}

// There are two versions
// One adds up the colors from all the iterations, the other uses only the last.
void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
        vec2 uv = ((2*fragCoord.xy - iResolution.xy)/ iResolution.x)/size;

        float a = iTime*speed;
        vec4 color = vec4(0.0);
        for (float i = 1.0; i < 10.0; i += 1.0) {
                uv = vec2(sin(a)*uv.y - cos(a)*uv.x, sin(a)*uv.x + cos(a)*uv.y);
                uv = mirror(uv);

                // These two lines can be changed for slightly different effects
                // This is just something simple that looks nice
                a += i;
                a /= i;
                if (addup) {
			color += texture(iChannel0, mirror(uv*vec2(1.,iResolution.x/iResolution.y)*2.0)) * 10.0/i;
		}
        }
	if (addup) {
        fragColor = color / 28.289;
	} else {
        fragColor = texture(iChannel0, mirror(uv*vec2(1.,iResolution.x/iResolution.y)*2.0));
	}
}
