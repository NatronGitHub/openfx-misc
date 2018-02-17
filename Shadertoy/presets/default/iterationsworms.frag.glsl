// https://www.shadertoy.com/view/ldl3W4

// I played a bit more with noisy iterations and got this

// iChannel0: Source, filter=mipmap, wrap=repeat

// Created by inigo quilez - iq/2013
// License Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License.

float hash( float n )
{
    return fract(sin(n)*43758.5453);
}

float noise( in vec2 x )
{
    vec2 p = floor(x);
    vec2 f = fract(x);
    f = f*f*(3.0-2.0*f);
    float n = p.x + p.y*57.0;
    return mix(mix( hash(n+  0.0), hash(n+  1.0),f.x),
               mix( hash(n+ 57.0), hash(n+ 58.0),f.x),f.y);
}

const mat2 ma = mat2( 1.6, -1.2, 1.2, 1.6 );

vec2 map( vec2 p )
{
	float a  = 0.50*noise(p)*6.2831*10.0; p=ma*p;
	      a += 0.35*noise(p)*6.2831*10.0; p=ma*p; 
	      a += 0.15*noise(p)*6.2831*10.0; p=ma*p; 
	a += 0.15*iTime;
	
	return vec2( cos(a), sin(a) );
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 p = fragCoord.xy / iResolution.xy;
	vec2 uv = -1.0 + 2.0*p;
	uv.x *= iResolution.x / iResolution.y;
	vec2 or = uv;
	
	float acc = 0.0;
	vec3  col = vec3(0.0);
	for( int i=0; i<64; i++ )
	{
		vec2 dir = map( uv );
		
		float h = float(i)/64.0;
		float w = 1.0-h;
		
		vec3 ttt = w*mix( 0.4*vec3(0.4,0.55,0.65), vec3(1.0,0.9,0.8), 0.5 + 0.5*dot( dir, -vec2(0.707) ) );
		
		col += w*ttt;
		acc += w;
		
		uv += 0.002*dir;
	}
	col /= acc;
    
	col = col*texture2D( iChannel0, uv  ).xyz;
	
    // extra fake lighting
	float ll = length( uv-or );
	vec3 nor = normalize( vec3(dFdx(ll), 4.0/iResolution.x, dFdy(ll) ) );
	col += vec3(1.0,0.5,0.2)*0.15*dot(nor,normalize(vec3(0.8,0.2,-0.8)) );
	col += 0.1*pow(nor.y,32.0);
	col += vec3(1.0,0.6,0.4)*col*0.2*(1.0-pow(nor.y,8.0));
	col *= 6.0*ll;
	col *= 2.0;
		
	col *= 0.8 + 0.2*pow( 16.0*p.x*p.y*(1.0-p.x)*(1.0-p.y), 0.25 );
	
	fragColor = vec4( col, 1.0 );
}
