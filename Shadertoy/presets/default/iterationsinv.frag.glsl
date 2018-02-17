// https://www.shadertoy.com/view/XdXGDS

// Playing with 16 inversions of the plane (1/z).

// Created by inigo quilez - iq/2013
// License Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License.

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 uv = fragCoord.xy/iResolution.xy;

    // shape (16 points)	
	float time = iTime + 47.0;
	vec2 z = -1.0 + 2.0*uv;
	vec3 col = vec3(1.0);
	for( int j=0; j<16; j++ )
	{
        // deform		
        float s = float(j)/16.0;
		float f = 0.2*(0.5 + 1.0*fract(sin(s*113.1)*43758.5453123));
		vec2 c = 0.5*vec2( cos(f*time+17.0*s),sin(f*time+19.0*s) );
		z -= c;
		float zr = length( z );
	    float ar = atan( z.y, z.x ) + zr*0.6;
	    z  = vec2( cos(ar), sin(ar) )/zr;
		z += c;
        z += 0.05*sin(2.0*z.x);

        // color		
        col -= 0.7*exp( -8.0*dot(z,z) )* (0.5+0.5*sin( 4.2*s + vec3(1.6,0.9,0.3) ));
	}
    col *= 0.75 + 0.25*clamp(length(z-uv)*0.6,0.0,1.0);

	// 3d effect
	float h = dot(col,vec3(0.333));
	vec3 nor = normalize( vec3( dFdx(h), dFdy(h), 1.0/iResolution.x ) );
	col -= 0.05*vec3(1.0,0.9,0.5)*dot(nor,vec3(0.8,0.4,0.2));;
	col += 0.25*(1.0-0.8*col)*nor.z*nor.z;

    // 2d postpro	
	col *= 1.12;
    col = pow( clamp(col,0.0,1.0), vec3(0.8) );
	col *= 0.8 + 0.2*pow( 16.0*uv.x*uv.y*(1.0-uv.x)*(1.0-uv.y), 0.1 );
	fragColor = vec4( col, 1.0 );
}
