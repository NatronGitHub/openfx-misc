// See: https://www.shadertoy.com/view/MsXSzM

// Wanted to test this funky square pattern before applying it in a raymarched tunnel.. which I hope is coming soon

// iChannel0: Source (tex15.png or any other image), filter=nearest, wrap=repeat

#define TIMESCALE 0.25
#define TILES 8
#define COLOR 0.7, 1.6, 2.8

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec2 uv = fragCoord.xy / iResolution.xy;
	uv.x *= iResolution.x / iResolution.y;
	
	vec4 noise = texture2D(iChannel0, floor(uv * float(TILES)) / float(TILES));
	float p = 1.0 - mod(noise.r + noise.g + noise.b + iTime * float(TIMESCALE), 1.0);
	p = min(max(p * 3.0 - 1.8, 0.1), 2.0);
	
	vec2 r = mod(uv * float(TILES), 1.0);
	r = vec2(pow(r.x - 0.5, 2.0), pow(r.y - 0.5, 2.0));
	p *= 1.0 - pow(min(1.0, 12.0 * dot(r, r)), 2.0);
	
	fragColor = vec4(COLOR, 1.0) * p;
}
