// https://www.shadertoy.com/view/lssGD7

// An old cathodic TV snow effect.

// iChannel0: Source, filter=linear, wrap=clamp
// BBox: iChannel0

float rand(vec2 co){
    return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec2 uv = fragCoord.xy / iResolution.xy;
	float screenRatio = iResolution.x / iResolution.y;
	
	vec3 texture = texture2D(iChannel0, uv).rgb;
	
	float barHeight = 6.;
	float barSpeed = 5.6;
	float barOverflow = 1.2;
	float blurBar = clamp(sin(uv.y * barHeight + iTime * barSpeed) + 1.25, 0., 1.);
	float bar = clamp(floor(sin(uv.y * barHeight + iTime * barSpeed) + 1.95), 0., barOverflow);
	
	float noiseIntensity = .75;
	float pixelDensity = 250.;
	vec3 color = vec3(clamp(rand(
		vec2(floor(uv.x * pixelDensity * screenRatio), floor(uv.y * pixelDensity)) *
		iTime / 1000.
	) + 1. - noiseIntensity, 0., 1.));
	
	color = mix(color - noiseIntensity * vec3(.25), color, blurBar);
	color = mix(color - noiseIntensity * vec3(.08), color, bar);
	color = mix(vec3(0.), texture, color);
	color.b += .042;
	
	color *= vec3(1.0 - pow(distance(uv, vec2(0.5, 0.5)), 2.1) * 2.8);
	
	fragColor = vec4(color, 1.);
}
