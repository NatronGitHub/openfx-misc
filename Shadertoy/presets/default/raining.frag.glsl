// https://www.shadertoy.com/view/4dXSzB

// Get your umbrellas ready, because I'm testing out texture2D!
// Yeah, just wanted to check how I could use the iChannels.
// BTW, use iChannel1 to switch the distortion texture for different results!

// iChannel0: Source, filter=mipmap, wrap=repeat
// iChannel1: Distortion (The distortion texture, try with a Rand), filter=mipmap, wrap=repeat
// BBox: iChannel0

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec2 uv = fragCoord.xy / iResolution.xy;
	float time = iTime;
	vec3 raintex = texture2D(iChannel1,vec2(uv.x*2.0,uv.y*0.1+time*0.125)).rgb/8.0;
	vec2 where = (uv.xy-raintex.xy);
	vec3 texchur1 = texture2D(iChannel0,vec2(where.x,where.y)).rgb;
	
	fragColor = vec4(texchur1,1.0);
}
