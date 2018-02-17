// https://www.shadertoy.com/view/4s23zz

// Warp. Tutorial here: http://www.iquilezles.org/www/articles/warp/warp.htm

// iChannel0: Rand (The output of a Rand plugin with Static Seed checked, or tex12.png), filter=linear, wrap=repeat

// Created by inigo quilez - iq/2013
// License Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License.

// See http://www.iquilezles.org/www/articles/warp/warp.htm for details

float noise( in vec2 x )
{
    vec2 p = floor(x);
    vec2 f = fract(x);
    f = f*f*(3.0-2.0*f);
    float a = texture2D(iChannel0,(p+vec2(0.5,0.5))/256.0).x;
	float b = texture2D(iChannel0,(p+vec2(1.5,0.5))/256.0).x;
	float c = texture2D(iChannel0,(p+vec2(0.5,1.5))/256.0).x;
	float d = texture2D(iChannel0,(p+vec2(1.5,1.5))/256.0).x;
    return mix(mix( a, b,f.x), mix( c, d,f.x),f.y);
}

const mat2 mtx = mat2( 0.80,  0.60, -0.60,  0.80 );

float fbm4( vec2 p )
{
    float f = 0.0;

    f += 0.5000*(-1.0+2.0*noise( p )); p = mtx*p*2.02;
    f += 0.2500*(-1.0+2.0*noise( p )); p = mtx*p*2.03;
    f += 0.1250*(-1.0+2.0*noise( p )); p = mtx*p*2.01;
    f += 0.0625*(-1.0+2.0*noise( p ));

    return f/0.9375;
}

float fbm6( vec2 p )
{
    float f = 0.0;

    f += 0.500000*noise( p ); p = mtx*p*2.02;
    f += 0.250000*noise( p ); p = mtx*p*2.03;
    f += 0.125000*noise( p ); p = mtx*p*2.01;
    f += 0.062500*noise( p ); p = mtx*p*2.04;
    f += 0.031250*noise( p ); p = mtx*p*2.01;
    f += 0.015625*noise( p );

    return f/0.96875;
}

float func( vec2 q, out vec2 o, out vec2 n )
{
    float ql = length( q );
    q.x += 0.05*sin(0.11*iTime+ql*4.0);
    q.y += 0.05*sin(0.13*iTime+ql*4.0);
    q *= 0.7 + 0.2*cos(0.05*iTime);

    q = (q+1.0)*0.5;

    o.x = 0.5 + 0.5*fbm4( vec2(2.0*q*vec2(1.0,1.0)          )  );
    o.y = 0.5 + 0.5*fbm4( vec2(2.0*q*vec2(1.0,1.0)+vec2(5.2))  );

    float ol = length( o );
    o.x += 0.02*sin(0.11*iTime*ol)/ol;
    o.y += 0.02*sin(0.13*iTime*ol)/ol;


    n.x = fbm6( vec2(4.0*o*vec2(1.0,1.0)+vec2(9.2))  );
    n.y = fbm6( vec2(4.0*o*vec2(1.0,1.0)+vec2(5.7))  );

    vec2 p = 4.0*q + 4.0*n;

    float f = 0.5 + 0.5*fbm4( p );

    f = mix( f, f*f*f*3.5, f*abs(n.x) );

    float g = 0.5+0.5*sin(4.0*p.x)*sin(4.0*p.y);
    f *= 1.0-0.5*pow( g, 8.0 );

    return f;
}

float funcs( in vec2 q )
{
    vec2 t1, t2;
    return func(q,t1,t2);
}


void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec2 p = fragCoord.xy / iResolution.xy;
	vec2 q = (-iResolution.xy + 2.0*fragCoord.xy) /iResolution.y;
	
    vec2 o, n;
    float f = func(q, o, n);
    vec3 col = vec3(0.0);


    col = mix( vec3(0.2,0.1,0.4), vec3(0.3,0.05,0.05), f );
    col = mix( col, vec3(0.9,0.9,0.9), dot(n,n) );
    col = mix( col, vec3(0.5,0.2,0.2), 0.5*o.y*o.y );


    col = mix( col, vec3(0.0,0.2,0.4), 0.5*smoothstep(1.2,1.3,abs(n.y)+abs(n.x)) );

    col *= f*2.0;
#if 1
    vec2 ex = vec2( 1.0 / iResolution.x, 0.0 );
    vec2 ey = vec2( 0.0, 1.0 / iResolution.y );
	vec3 nor = normalize( vec3( funcs(q+ex) - f, ex.x, funcs(q+ey) - f ) );
#else
    vec3 nor = normalize( vec3( dFdx(f)*iResolution.x, 1.0, dFdy(f)*iResolution.y ) );	
#endif
    vec3 lig = normalize( vec3( 0.9, -0.2, -0.4 ) );
    float dif = clamp( 0.3+0.7*dot( nor, lig ), 0.0, 1.0 );

    vec3 bdrf;
    bdrf  = vec3(0.85,0.90,0.95)*(nor.y*0.5+0.5);
    bdrf += vec3(0.15,0.10,0.05)*dif;

    bdrf  = vec3(0.85,0.90,0.95)*(nor.y*0.5+0.5);
    bdrf += vec3(0.15,0.10,0.05)*dif;

    col *= bdrf;

    col = vec3(1.0)-col;

    col = col*col;

    col *= vec3(1.2,1.25,1.2);
	
	col *= 0.5 + 0.5 * sqrt(16.0*p.x*p.y*(1.0-p.x)*(1.0-p.y));
	
	fragColor = vec4( col, 1.0 );
}
