// https://www.shadertoy.com/view/MdfGD2

// mouse-xpos to vary intensity
// First glitch effect from the demo 'shampain': http://www.pouet.net/prod.php?which=60539

// iChannel0: Source, filter=linear, wrap=clamp
// BBox: iChannel0

float sat( float t ) {
	return clamp( t, 0.0, 1.0 );
}
vec2 sat( vec2 t ) {
	return clamp( t, 0.0, 1.0 );
}
// vec3 sat( vec3 v ) {
	// return clamp( v, 0.0f, 1.0f );
// }

//remaps inteval [a;b] to [0;1]
float remap( float t, float a, float b ) {
	return sat( (t - a) / (b - a) );
}

//note: /\ t=[0;0.5;1], y=[0;1;0]
float linterp( float t ) {
	return sat( 1.0 - abs( 2.0*t - 1.0 ) );
}

//note: [0;1]
float rand( vec2 n ) {
  return fract(sin(dot(n.xy, vec2(12.9898, 78.233)))* 43758.5453);
}

//note: [-1;1]
float srand( vec2 n ) {
	return rand(n) * 2.0 - 1.0;
}

float trunc( float x, float num_levels )
{
	return floor(x*num_levels) / num_levels;
}
vec2 trunc( vec2 x, vec2 num_levels )
{
	return floor(x*num_levels) / num_levels;
}

vec3 rgb2yuv( vec3 rgb )
{
	vec3 yuv;
	yuv.x = dot( rgb, vec3(0.299,0.587,0.114) );
	yuv.y = dot( rgb, vec3(-0.14713, -0.28886, 0.436) );
	yuv.z = dot( rgb, vec3(0.615, -0.51499, -0.10001) );
	return yuv;
 }
 vec3 yuv2rgb( vec3 yuv )
 {
	vec3 rgb;
	rgb.r = yuv.x + yuv.z * 1.13983;
	rgb.g = yuv.x + dot( vec2(-0.39465, -0.58060), yuv.yz );
	rgb.b = yuv.x + yuv.y * 2.03211;
	return rgb;
 }

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	float THRESHOLD = 0.1 + iMouse.x / iResolution.x;
	float time_s = mod( iTime, 32.0 );

	float glitch_threshold = 1.0 - THRESHOLD;
	const float max_ofs_siz = 0.1; //TOOD: input
	const float yuv_threshold = 0.5; //TODO: input, >1.0f == no distort
	const float time_frq = 16.0;

	vec2 uv = fragCoord.xy / iResolution.xy;
    uv.y = 1.0 -  uv.y;
	
	const float min_change_frq = 4.0;
	float ct = trunc( time_s, min_change_frq );
	float change_rnd = rand( trunc(uv.yy,vec2(16)) + 150.0 * ct );

	float tf = time_frq*change_rnd;

	float t = 5.0 * trunc( time_s, tf );
	float vt_rnd = 0.5*rand( trunc(uv.yy + t, vec2(11)) );
	vt_rnd += 0.5 * rand(trunc(uv.yy + t, vec2(7)));
	vt_rnd = vt_rnd*2.0 - 1.0;
	vt_rnd = sign(vt_rnd) * sat( ( abs(vt_rnd) - glitch_threshold) / (1.0-glitch_threshold) );

	vec2 uv_nm = uv;
	uv_nm = sat( uv_nm + vec2(max_ofs_siz*vt_rnd, 0) );

	float rnd = rand( vec2( trunc( time_s, 8.0 )) );
	uv_nm.y = (rnd>mix(1.0, 0.975, sat(THRESHOLD))) ? 1.0-uv_nm.y : uv_nm.y;

	vec4 sample = texture2D( iChannel0, uv_nm, -10.0 );
	vec3 sample_yuv = rgb2yuv( sample.rgb );
	sample_yuv.y /= 1.0-3.0*abs(vt_rnd) * sat( yuv_threshold - vt_rnd );
	sample_yuv.z += 0.125 * vt_rnd * sat( vt_rnd - yuv_threshold );
	fragColor = vec4( yuv2rgb(sample_yuv), sample.a );
}
