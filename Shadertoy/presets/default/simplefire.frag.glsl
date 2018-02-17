// https://www.shadertoy.com/view/ldXGRl

// Simpler and faster fire shader based on @301z's fire shader

// much simplified version of the fire shader by @301z

const float SCALE = 16.0;
// speed of perturbations
const float PSPEED1 = 1.5;
const float PSPEED2 = 1.0;
// speed of flame cores
const vec2 CORESPEED = vec2(0.2,0.4);
const vec3 color1 = vec3(0.5, 0.0, 0.0);
const vec3 color2 = vec3(1.0, 0.5, 0.0);

float rand(vec2 n) { 
	return fract(sin(dot(n, vec2(12.9898, 4.1414))) * 43758.5453);
}

float noise(vec2 n) {
	const vec2 d = vec2(0.0, 1.0);
	vec2 b = floor(n);
	vec2 f = smoothstep(vec2(0.0), vec2(1.0), fract(n));
	return mix(mix(rand(b), rand(b + d.yx), f.x), mix(rand(b + d.xy), rand(b + d.yy), f.x), f.y);
}

void mainImage( out vec4 fragColor, in vec2 fragCoord ) {
	vec2 p = fragCoord.xy * SCALE / iResolution.xx;
	vec2 r = vec2(noise(p + iTime*PSPEED1 - p.x - p.y), noise(p - iTime*PSPEED2));
	vec3 c = mix(color1, color2, noise(p + r - CORESPEED*iTime));
	fragColor = vec4(c, 1.0);
}

