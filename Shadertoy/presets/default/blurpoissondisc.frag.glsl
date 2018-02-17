// https://www.shadertoy.com/view/lsfGWn

// Just a regular poisson-disc blur.
// (bottom picking from mipmap instead)

// iChannel0: Source, filter=mipmap, wrap=repeat
// BBox: iChannel0

#define ANIMATE_NOISE

float nrand( vec2 n ) {
	return fract(sin(dot(n.xy, vec2(12.9898, 78.233)))* 43758.5453);
}

vec2 rot2d( vec2 p, float a ) {
	vec2 sc = vec2(sin(a),cos(a));
	return vec2( dot( p, vec2(sc.y, -sc.x) ), dot( p, sc.xy ) );
}

const int NUM_TAPS = 27;
const float rcp_maxdist = 1.0 / 4.22244;

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{

    vec2 uv = fragCoord.xy / iResolution.xy;
	uv.y = 1.0-uv.y;
   
    float max_siz;
    if ( iMouse.z > 0.5 )
		max_siz = 32.0 * (1.0-iMouse.x / iResolution.x); // * (0.5+0.5*sin(iTime));
    else
        max_siz = 32.0 * (0.5+0.5*sin(2.0*uv.x + iTime));
        
    //fragColor = vec4( vec3(max_siz), 1.0 );
    //return;
	
    //note: for samples-positions see
    //      https://github.com/GPUOpen-Effects/ShadowFX/blob/master/amd_shadowfx/src/Shaders/
    
	vec2 fTaps_Poisson[NUM_TAPS];
    fTaps_Poisson[0]  = rcp_maxdist * vec2(  -0.8835609, 2.523391 );
    fTaps_Poisson[1]  = rcp_maxdist * vec2(  -1.387375, 1.056318 );
    fTaps_Poisson[2]  = rcp_maxdist * vec2(  -2.854452, 1.313645 );
    fTaps_Poisson[3]  = rcp_maxdist * vec2(  0.6326182, 1.14569 );
    fTaps_Poisson[4]  = rcp_maxdist * vec2(  1.331515, 3.637297 );
    fTaps_Poisson[5]  = rcp_maxdist * vec2(  -2.175307, 3.885795 );
    fTaps_Poisson[6]  = rcp_maxdist * vec2(  -0.5396664, 4.1938 );
    fTaps_Poisson[7]  = rcp_maxdist * vec2(  -0.6708734, -0.36875 );
    fTaps_Poisson[8]  = rcp_maxdist * vec2(  -2.083908, -0.6921188 );
    fTaps_Poisson[9]  = rcp_maxdist * vec2(  -3.219028, 2.85465 );
    fTaps_Poisson[10] = rcp_maxdist * vec2(  -1.863933, -2.742254 );
    fTaps_Poisson[11] = rcp_maxdist * vec2(  -4.125739, -1.283028 );
    fTaps_Poisson[12] = rcp_maxdist * vec2(  -3.376766, -2.81844 );
    fTaps_Poisson[13] = rcp_maxdist * vec2(  -3.974553, 0.5459405 );
    fTaps_Poisson[14] = rcp_maxdist * vec2(  3.102514, 1.717692 );
    fTaps_Poisson[15] = rcp_maxdist * vec2(  2.951887, 3.186624 );
    fTaps_Poisson[16] = rcp_maxdist * vec2(  1.33941, -0.166395 );
    fTaps_Poisson[17] = rcp_maxdist * vec2(  2.814727, -0.3216669 );
    fTaps_Poisson[18] = rcp_maxdist * vec2(  0.7786853, -2.235639 );
    fTaps_Poisson[19] = rcp_maxdist * vec2(  -0.7396695, -1.702466 );
    fTaps_Poisson[20] = rcp_maxdist * vec2(  0.4621856, -3.62525 );
    fTaps_Poisson[21] = rcp_maxdist * vec2(  4.181541, 0.5883132 );
    fTaps_Poisson[22] = rcp_maxdist * vec2(  4.22244, -1.11029 );
    fTaps_Poisson[23] = rcp_maxdist * vec2(  2.116917, -1.789436 );
    fTaps_Poisson[24] = rcp_maxdist * vec2(  1.915774, -3.425885 );
    fTaps_Poisson[25] = rcp_maxdist * vec2(  3.142686, -2.656329 );
    fTaps_Poisson[26] = rcp_maxdist * vec2(  -1.108632, -4.023479 );


    if ( uv.y > 0.5 )
    {
        fragColor = texture2D( iChannel0, uv, log2(max_siz) );
    }
    else
    {
        vec4 sum = vec4(0);
        vec2 seed = uv;
        #if defined( ANIMATE_NOISE )
        seed += fract( iTime );
        #endif
        float rnd = 6.28 * nrand( seed );

        vec4 basis = vec4( rot2d(vec2(1,0),rnd), rot2d(vec2(0,1),rnd) );
        for (int i=0; i < NUM_TAPS; i++)
        {
            vec2 ofs = fTaps_Poisson[i]; ofs = vec2(dot(ofs,basis.xz),dot(ofs,basis.yw) );
            //vec2 ofs = rot2d( fTaps_Poisson[i], rnd );
            vec2 texcoord = uv + max_siz * ofs / iResolution.xy;
            sum += texture2D(iChannel0, texcoord, -10.0);
        }

        fragColor = sum / vec4(NUM_TAPS);
    }
    
}
