// from https://www.shadertoy.com/view/Xdl3D2

// March through a 2D grid, offsetting stars along z for each grid cell. This is much faster than doing a loop over all stars, but creates some artefacts.

// iChannel0: Rand (The output of a Rand plugin with Static Seed checked, or tex16.png), filter=mipmap, wrap=repeat

#ifdef GL_ES
precision lowp float;
#endif

vec4 Noise( in ivec2 x )
{
	return texture2D( iChannel0, (vec2(x)+0.5)/256.0, -100.0 );
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec3 ray;
	ray.xy = 2.0*(fragCoord.xy-iResolution.xy*.5)/iResolution.x;
	ray.z = 1.0;

	float offset = iTime*.5;	
	float speed2 = (cos(offset)+1.0)*2.0;
	float ispeed = 1.0/(speed2+.1);
	offset += sin(offset)*.96;
	offset *= 2.0;

	vec3 col = vec3(0);

	vec3 stp = ray/max(abs(ray.x),abs(ray.y));

	vec3 pos = 2.0*stp+.5;
	for ( int i=0; i < 14; i++ )
	{
		float z = Noise(ivec2(pos.xy)).x;
		z = fract(z-offset);
		float d = 50.0*z-pos.z;
		float w = max(0.0,1.0-8.0*length(fract(pos.xy)-.5));
                w *= w;
		vec3 c = max(vec3(0),vec3(1.0-abs(d+speed2*.5)*ispeed,1.0-abs(d)*ispeed,1.0-abs(d-speed2*.5)*ispeed));
		col += 1.5*(1.0-z)*c*w;
		pos += stp;
	}
	
	fragColor = vec4(col,1.0);
}
