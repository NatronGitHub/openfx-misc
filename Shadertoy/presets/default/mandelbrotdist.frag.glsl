// https://www.shadertoy.com/view/lsX3W4

// Distance estimation to the Mandelbrot set. Basically, distance(c) = |G(c)|/|G'(c))|. More info in http://www.iquilezles.org/www/articles/distancefractals/distancefractals.htm

// Created by inigo quilez - iq/2013
// License Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License.


// This shader computes the distance to the Mandelbrot Set for everypixel, and colorizes
// it accoringly.
// 
// Z -> ZÂ²+c, Z0 = 0. 
// therefore Z' -> 2Â·ZÂ·Z' + 1
//
// The Hubbard-Douady potential G(c) is G(c) = log Z/2^n
// G'(c) = Z'/Z/2^n
//
// So the distance is |G(c)|/|G'(c)| = |Z|Â·log|Z|/|Z'|
//
// More info here: http://www.iquilezles.org/www/articles/distancefractals/distancefractals.htm


void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 p = -1.0 + 2.0 * fragCoord.xy / iResolution.xy;
    p.x *= iResolution.x/iResolution.y;

    // animation	
	float tz = 0.5 - 0.5*cos(0.225*iTime);
    float zoo = pow( 0.5, 13.0*tz );
	vec2 c = vec2(-0.05,.6805) + p*zoo;

    // iterate
    vec2 z  = vec2(0.0);
    float m2 = 0.0;
    vec2 dz = vec2(0.0);
    for( int i=0; i<256; i++ )
    {
        if( m2>1024.0 ) continue;

		// Z' -> 2Â·ZÂ·Z' + 1
        dz = 2.0*vec2(z.x*dz.x-z.y*dz.y, z.x*dz.y + z.y*dz.x) + vec2(1.0,0.0);
			
        // Z -> ZÂ² + c			
        z = vec2( z.x*z.x - z.y*z.y, 2.0*z.x*z.y ) + c;
			
        m2 = dot(z,z);
    }

    // distance	
	// d(c) = |Z|Â·log|Z|/|Z'|
	float d = 0.5*sqrt(dot(z,z)/dot(dz,dz))*log(dot(z,z));

	
    // do some soft coloring based on distance
	d = clamp( 8.0*d/zoo, 0.0, 1.0 );
	d = pow( d, 0.25 );
    vec3 col = vec3( d );
    
    fragColor = vec4( col, 1.0 );
}
