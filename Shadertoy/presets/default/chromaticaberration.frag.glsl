// https://www.shadertoy.com/view/Mds3zn

// Some kind of camera/transmission interference

// iChannel0: Source, filter=linear, wrap=clamp
// BBox: iChannel0

uniform bool animated = false; // Animated
uniform float amount_max = 0.05; // Amount, min=0., max=0.1

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 uv = fragCoord.xy / iResolution.xy;

    float amount = 1.0;
    if (animated) {
        amount = (1.0 + sin(iTime*6.0)) * 0.5;
        amount *= 1.0 + sin(iTime*16.0) * 0.5;
        amount *= 1.0 + sin(iTime*19.0) * 0.5;
        amount *= 1.0 + sin(iTime*27.0) * 0.5;
        amount = pow(amount, 3.0);
    }
    amount *= amount_max;
	
    vec3 col;
    col.r = texture2D( iChannel0, vec2(uv.x+amount,uv.y) ).r;
    col.g = texture2D( iChannel0, uv ).g;
    col.b = texture2D( iChannel0, vec2(uv.x-amount,uv.y) ).b;

    col *= (1.0 - amount * 0.5);
	
    fragColor = vec4(col,1.0);
}

