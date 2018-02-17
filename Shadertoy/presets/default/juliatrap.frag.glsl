// https://www.shadertoy.com/view/4d23WG

// Minimun code showing orbit trapping (line 21) for coloring fractals (and other dynamic systems)

// max_iterations was 17 in the screensaver version
uniform int max_iterations = 128; // Max. Iterations (Maximum number of iterations), min=0, max=500

// Created by inigo quilez - iq/2014
// License Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License.

// line 13: pixel coordinates	
// line 15: c travels around the main cardiod c(t) = ½e^it - ¼e^i2t
// line 20: z = z² + c
// line 21: trap orbit
// line 24: remap	
// line 26: color	

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec2 z = 1.15*(-iResolution.xy+2.0*fragCoord.xy)/iResolution.y;

	vec2 an = 0.51*cos( vec2(0.0,1.5708) + 0.1*iTime ) - 0.25*cos( vec2(0.0,1.5708) + 0.2*iTime );

	for( int i=0; i<max_iterations; i++ )
	{
		z = vec2( z.x*z.x-z.y*z.y, 2.0*z.x*z.y ) + an;
	}
	
	float f = min(1e20, dot(z,z));
	f = 1.0+log(f)/16.0;

	fragColor = vec4(f,f*f,f*f*f,1.0);
}
