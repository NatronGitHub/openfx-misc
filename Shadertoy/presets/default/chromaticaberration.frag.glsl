// https://www.shadertoy.com/view/Mds3zn

// Some kind of camera/transmission interference

// iChannel0: Source, filter=linear, wrap=clamp

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 uv = fragCoord.xy / iResolution.xy;

	float amount = 0.0;
	
	amount = (1.0 + sin(iGlobalTime*6.0)) * 0.5;
	amount *= 1.0 + sin(iGlobalTime*16.0) * 0.5;
	amount *= 1.0 + sin(iGlobalTime*19.0) * 0.5;
	amount *= 1.0 + sin(iGlobalTime*27.0) * 0.5;
	amount = pow(amount, 3.0);

	amount *= 0.05;
	
    vec3 col;
    col.r = texture2D( iChannel0, vec2(uv.x+amount,uv.y) ).r;
    col.g = texture2D( iChannel0, uv ).g;
    col.b = texture2D( iChannel0, vec2(uv.x-amount,uv.y) ).b;

	col *= (1.0 - amount * 0.5);
	
    fragColor = vec4(col,1.0);
}

