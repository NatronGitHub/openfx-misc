// https://www.shadertoy.com/view/4dlGDN

// Or a line integral, pretty much inspired by fizzer's "funcky planet". I used to know this as "directional blur" in the demoscene times. Here it is anyway, a doodling that ended up looking cute I think ^_^

// iChannel0: Source, filter=mipmap, wrap=repeat
// BBox: iChannel0

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

vec2 map( vec2 p, in float offset )
{
	p.x += 0.1*sin( iTime + 2.0*p.y ) ;
	p.y += 0.1*sin( iTime + 2.0*p.x ) ;
	
	float a = noise(p*1.5 + sin(0.1*iTime))*6.2831;
	a -= offset;
	return vec2( cos(a), sin(a) );
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 p = fragCoord.xy / iResolution.xy;
	vec2 uv = -1.0 + 2.0*p;
	uv.x *= iResolution.x / iResolution.y;
		
    float offset = iTime + fragCoord.x/iResolution.x;
    
	float acc = 0.0;
	vec3  col = vec3(0.0);
	for( int i=0; i<32; i++ )
	{
		vec2 dir = map( uv, offset );
		
		float h = float(i)/32.0;
		float w = 4.0*h*(1.0-h);
		
		vec3 ttt = w*texture2D( iChannel0, uv ).xyz;
		ttt *= mix( vec3(0.6,0.7,0.7), vec3(1.0,0.95,0.9), 0.5 - 0.5*dot( reflect(vec3(dir,0.0), vec3(1.0,0.0,0.0)).xy, vec2(0.707) ) );
		col += w*ttt;
		acc += w;
		
		uv += 0.008*dir;
	}
	col /= acc;
    
	float gg = dot( col, vec3(0.333) );
	vec3 nor = normalize( vec3( dFdx(gg), 0.5, dFdy(gg) ) );
	col += vec3(0.4)*dot( nor, vec3(0.7,0.01,0.7) );

	vec2 di = map( uv, offset );
	col *= 0.65 + 0.35*dot( di, vec2(0.707) );
	col *= 0.20 + 0.80*pow( 4.0*p.x*(1.0-p.x), 0.1 );
	col *= 1.7;

	fragColor = vec4( col, 1.0 );
}
