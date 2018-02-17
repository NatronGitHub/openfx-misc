// https://www.shadertoy.com/view/Xsl3zn

// Using a texture's range to deform its own domain

// iChannel0: Source, filter=mipmap, wrap=repeat

// Created by inigo quilez - iq/2013
// License Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License.

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec2 uv = 0.5*fragCoord/iResolution.xy;

	float d = length(uv);
	vec2 st = uv*0.1 + 0.2*vec2(cos(0.071*iTime+d),
								sin(0.073*iTime-d));

    vec3 col = texture2D( iChannel0, st ).xyz;
	col *= col.x*2.0;
	col *= 1.0 - texture2D( iChannel0, 0.4*uv + 0.1*col.xy  ).xyy;
	col *= 1.0 + 2.0*d;
    
	fragColor = vec4( col, 1.0 );
}
