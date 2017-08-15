// Created by inigo quilez - iq/2014
// License Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License.						    

// The sixth member of the "Iterations" collection:
//
// Inversion:     https://www.shadertoy.com/view/XdXGDS
// Worms:         https://www.shadertoy.com/view/ldl3W4
// Coral:         https://www.shadertoy.com/view/4sXGDN
// Guts:          https://www.shadertoy.com/view/MssGW4
// Trigonometric: https://www.shadertoy.com/view/Mdl3RH

vec3 shape( in vec2 p )
{
	p *= 2.0;
	
	vec3 s = vec3( 0.0 );
	vec2 z = p;
	for( int i=0; i<8; i++ ) 
	{
        // transform		
		z += cos(z.yx + cos(z.yx + cos(z.yx+0.5*iTime) ) );

        // orbit traps		
		float d = dot( z-p, z-p ); 
		s.x += 1.0/(1.0+d);
		s.y += d;
		s.z += sin(atan(z.y-p.y,z.x-p.x));
		
	}
	
	return s / 8.0;
}

void mainImage( out vec4 fragColor, in vec2 fragCoord ) 
{
	vec2 pc = (2.0*fragCoord.xy-iResolution.xy)/min(iResolution.y,iResolution.x);

	vec2 pa = pc + vec2(0.04,0.0);
	vec2 pb = pc + vec2(0.0,0.04);
	
    // shape (3 times for diferentials)	
	vec3 sc = shape( pc );
	vec3 sa = shape( pa );
	vec3 sb = shape( pb );

    // color	
	vec3 col = mix( vec3(0.08,0.02,0.15), vec3(0.6,1.1,1.6), sc.x );
	col = mix( col, col.zxy, smoothstep(-0.5,0.5,cos(0.5*iTime)) );
	col *= 0.15*sc.y;
	col += 0.4*abs(sc.z) - 0.1;

    // light	
	vec3 nor = normalize( vec3( sa.x-sc.x, 0.01, sb.x-sc.x ) );
	float dif = clamp(0.5 + 0.5*dot( nor,vec3(0.5773) ),0.0,1.0);
	col *= 1.0 + 0.7*dif*col;
	col += 0.3 * pow(nor.y,128.0);

    // vignetting	
	col *= 1.0 - 0.1*length(pc);
	
	fragColor = vec4( col, 1.0 );
}