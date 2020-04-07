// https://www.shadertoy.com/view/4df3R7

// Single pass of circular blur. Can be multi-passed as described by Dmitry Andreev at GDC2013 for bokeh-like patterns.

// Adapted to Natron by F. Devernay

// iChannel0: Source, filter=linear, wrap=clamp
// iChannel1: Modulate (Image containing a factor to be applied to the Blur size in the first channel), filter=linear, wrap=clamp
// iChannel2: Dither (The tex15.png texture), filter=mipmap, wrap=repeat
// BBox: iChannel0

const vec2 iRenderScale = vec2(1.,1.);
const vec2 iChannelOffset[4] = vec2[4]( vec2(0.,0.), vec2(0.,0.), vec2(0.,0.), vec2(0.,0.) );
uniform float size = 10.; // Blur Size (Blur size in pixels), min=0., max=20.
uniform int samples = 16; // Samples (Number of samples - higher is better and slower), min=2, max=64
uniform bool perpixel_size = false; // Modulate (Modulate the blur size by multiplying it by the first channel of the Modulate input)

float nrand( vec2 n ) {
	return fract(sin(dot(n.xy, vec2(12.9898, 78.233)))* 43758.5453);
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec2 uv = fragCoord.xy / iResolution.xy;
    
	float maxofs = size;
	if (perpixel_size) {
		maxofs *= texture2D(iChannel1, (fragCoord.xy-iChannelOffset[1].xy)/iChannelResolution[1].xy).x;
	}
	const int NUM_SAMPLES_MAX = 64;
	int NUM_SAMPLES = int(min(samples,NUM_SAMPLES_MAX));
	int NUM_SAMPLES2 = NUM_SAMPLES/2;
	float NUM_SAMPLES_F = float(NUM_SAMPLES);
	float anglestep = 6.28 / NUM_SAMPLES_F;

	//note: rand
	float rnd = nrand( 0.01*fragCoord.xy + fract(iTime) );
	
	//note: ordered dither
	//float rnd = texture2D( iChannel2, fragCoord.xy / 8.0 ).r;
    
	//note: create halfcircle of offsets
	vec2 ofs[NUM_SAMPLES_MAX];
	{
		vec2 c1 = maxofs * iRenderScale / iResolution.xy;
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
		sum += texture2D( iChannel0, uv+ofs[i] );

	//note: sample negative half-circle
	for( int i=0;i<NUM_SAMPLES2;++i )
		sum += texture2D( iChannel0, uv-ofs[i] );

	fragColor = sum / NUM_SAMPLES_F;
}
