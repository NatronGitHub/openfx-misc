// https://www.shadertoy.com/view/4df3R7

// Single pass of circular blur. Can be multi-passed as described by Dmitry Andreev at GDC2013 for bokeh-like patterns.

// iChannel0: Source, filter=mipmap, wrap=repeat
// iChannel1: Dither (The tex15.png texture), filter=mipmap, wrap=repeat
// BBox: iChannel0

float nrand( vec2 n ) {
	return fract(sin(dot(n.xy, vec2(12.9898, 78.233)))* 43758.5453);
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec2 uv = fragCoord.xy / iResolution.xy;
    
    float ctrdist = length(vec2(0.5,0.5)-uv);
    
	uv.x += 0.1*iTime;

	float maxofs = 50.0 * (1.0-iMouse.x / iResolution.x); // n( iTime ));
	const int NUM_SAMPLES = 16;
	const int NUM_SAMPLES2 = NUM_SAMPLES/2;
	const float NUM_SAMPLES_F = float(NUM_SAMPLES);
	const float anglestep = 6.28 / NUM_SAMPLES_F;
	const float MIPBIAS = -8.0; //note: make sure we always pick mip0

	//note: rand
	float rnd = nrand( 0.01*fragCoord.xy + fract(iTime) );
	
	//note: ordered dither
	//float rnd = texture2D( iChannel1, fragCoord.xy / 8.0 ).r;
    
	//note: create halfcircle of offsets
	vec2 ofs[NUM_SAMPLES];
	{
		vec2 c1 = vec2(maxofs) / iResolution.xy;
        c1 *= pow(ctrdist, 1.5);
		float angle = 3.1416*rnd;
		for( int i=0;i<NUM_SAMPLES2;++i )
		{
			//ofs[i] = rot2d( vec2(maxofs,0.0), angle ) / iResolution.xy;
			ofs[i] = c1 * vec2( cos(angle), sin(angle) );
			angle += anglestep;
		}
	}
	
	vec4 sum = vec4(0.0);
	//note: sample positive half-circle
	for( int i=0;i<NUM_SAMPLES2;++i )
		sum += texture2D( iChannel0, vec2(uv.x,1.0-uv.y)+ofs[i], MIPBIAS );

	//note: sample negative half-circle
	for( int i=0;i<NUM_SAMPLES2;++i )
		sum += texture2D( iChannel0, vec2(uv.x,1.0-uv.y)-ofs[i], MIPBIAS );

	fragColor.rgb = sum.rgb / NUM_SAMPLES_F;
	fragColor.a = 1.0;
}
