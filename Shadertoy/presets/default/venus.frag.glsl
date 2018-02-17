// https://www.shadertoy.com/view/llsGWM

// Style variation of : https://www.shadertoy.com/view/ltXGD7

// iChannel0: Rand (The output of a Rand plugin with Static Seed checked, or tex16.png), filter=mipmap, wrap=repeat

#define X c += texture2D(iChannel0, p*.1 - t*.002); p *= .4; c *= .7;

void mainImage( out vec4 f, in vec2 w ) {
	vec2 p = iResolution.xy;
	float d = length(p = (w.xy*2.-p) / p.y), t = iTime;
    vec4 b = vec4(.8,.4,.2,1)+p.y, c = b+b;
    p = p * asin(d) / d + 5.;    
    p = p * p.y + t;
    X X X X X
	f = (c.g+(b-c.g)*c.r) * (1.5-d*d);
}

