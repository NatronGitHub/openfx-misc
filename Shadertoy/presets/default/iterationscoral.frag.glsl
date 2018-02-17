// https://www.shadertoy.com/view/4sXGDN

// Orbit trap coloring for noise-based iterations. Totally improvised.

// Created by inigo quilez - iq/2013
// License Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License.						    

float hash( vec2 p )
{
	float h = dot(p,vec2(127.1,311.7));
	
    return -1.0 + 2.0*fract(sin(h)*43758.5453123);
}

float noise( in vec2 p )
{
    vec2 i = floor( p );
    vec2 f = fract( p );
	
	vec2 u = f*f*(3.0-2.0*f);

    return mix( mix( hash( i + vec2(0.0,0.0) ), 
                     hash( i + vec2(1.0,0.0) ), u.x),
                mix( hash( i + vec2(0.0,1.0) ), 
                     hash( i + vec2(1.0,1.0) ), u.x), u.y);
}


vec2 iterate( in vec2 p, in vec4 t )
{
	float an  = noise(13.0*p)*3.1416;
	      an += noise(10.0*p)*3.1416;
	
	return p + 0.01*vec2(cos(an),sin(an));
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec2 q = fragCoord.xy / iResolution.xy;
	vec2 p = -1.0 + 2.0*q;
	p.x *= iResolution.x/iResolution.y;

	p *= 0.85;
	
	p *= 3.0 + 2.0*cos(3.1*iTime/10.0);	

	vec4 t = 0.15*iTime*vec4( 1.0, -1.5, 1.2, -1.6 ) + vec4(0.0,2.0,3.0,1.0);
	
    vec2 z = p;
	vec2 s = vec2(0.0);
	for( int i=0; i<100; i++ ) 
	{
		z = iterate( z, t );

		float d = dot( z-p, z-p ); 
		s.x += abs(p.x-z.x);
		s.y = max( s.y, d );
	}
    s.x /= 100.0;
	

	vec3 col = 0.5 + 0.5*cos( s.y*3.2 + 0.5+vec3(4.5,2.4,1.5) );
	col *= s.x*4.0;

	vec3 nor = normalize( vec3( dFdx(s.x), 0.001, dFdy(s.x) ) );
	col -= vec3(0.2)*dot( nor, vec3(0.7,0.1,0.7) );

	col *= 1.4*s.y;

	col = sqrt(col)-0.16;
	
	col += 0.3*s.x*s.y*noise(p*100.0 + 40.0*s.y);
	
	col *= vec3(1.0,1.,1.4);
	
	col *= 0.5 + 0.5*pow( 16.0*q.x*q.y*(1.0-q.x)*(1.0-q.y), 0.1 );

	fragColor = vec4( col, 1.0 );
}
