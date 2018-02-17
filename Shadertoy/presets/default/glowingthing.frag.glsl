// from https://www.shadertoy.com/view/4lB3DG

// My first nice-looking (or at least not-too-bad-looking) fragment shader. \o/

void mainImage(out vec4 f, vec2 u )
{
	u /= iResolution.xy;
    f.xy = .5 - u;

    float t = iTime,
          z = atan(f.y,f.x) * 3.,
          v = cos(z + sin(t * .1)) + .5 + sin(u.x * 10. + t * 1.3) * .4;

    f.x = 1.2 + cos(z - t*.2) + sin(u.y*10.+t*1.5)*.5;
	f.yz = vec2( sin(v*4.)*.25, sin(v*2.)*.3 ) + f.x*.5;
}
