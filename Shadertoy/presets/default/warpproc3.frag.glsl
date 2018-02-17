// https://www.shadertoy.com/view/XsfSD4

// In place warping

// Created by inigo quilez - iq/2014
// License Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License.

vec2 hash( vec2 p )
{
    p = vec2( dot(p,vec2(2127.1,81.17)), dot(p,vec2(1269.5,283.37)) );
	return fract(sin(p)*43758.5453);
}

float noise( in vec2 p )
{
    vec2 i = floor( p );
    vec2 f = fract( p );
	
	vec2 u = f*f*(3.0-2.0*f);

    float n = mix( mix( dot( -1.0+2.0*hash( i + vec2(0.0,0.0) ), f - vec2(0.0,0.0) ), 
                        dot( -1.0+2.0*hash( i + vec2(1.0,0.0) ), f - vec2(1.0,0.0) ), u.x),
                   mix( dot( -1.0+2.0*hash( i + vec2(0.0,1.0) ), f - vec2(0.0,1.0) ), 
                        dot( -1.0+2.0*hash( i + vec2(1.0,1.0) ), f - vec2(1.0,1.0) ), u.x), u.y);
	return 0.5 + 0.5*n;
}

float fbm( in vec2 x )
{
	float m = 0.0;
	vec2 p = 6.0*x;

	float w = 0.5;
	float s = 1.0;
	for( int i=0; i<8; i++ )
    {
		float n = noise( p*s );
		m += w*n;

        p.y -= 1.25*(-1.0+2.0*n) - 0.1*iTime*w; // warp
		
		w *= 0.5;
		s *= 2.0;

	}
	
    return m;	
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 p = fragCoord.xy/iResolution.x;

    // pattern	
	float n = fbm( p );

    // normal	
	vec2 e = vec2(0.03,0.0);
	vec3 nor = normalize( vec3( fbm(p-e.xy)-fbm(p+e.xy), 2.0*e.x, fbm(p-e.yx)-fbm(p+e.yx) ) );

    // color
	float c = pow(n,4.0)*6.0;		
	vec3 col = 0.5 + 0.5*cos( 5.0 + n*12.0 + vec3(0.8,0.4,0.0) );
    col *= 0.4 + 0.6*c;
	
    // light	
	vec3 lig = normalize( vec3(1.0,0.2,1.0) );
	col *= vec3(0.6,0.7,0.8) + 1.0*vec3(1.0,0.7,0.5)*clamp( 0.3 + 0.7*dot(nor,lig), 0.0, 1.0 );
    col += 1.5*vec3(1.0,0.5,0.0)*pow(1.0-nor.y,2.0)*c;	

    // postpro
	col = sqrt(col)-0.15;
    col *= sqrt(1.0-0.5*abs(p.x-0.5));	
	
    fragColor = vec4( col, 1.0 );
}
