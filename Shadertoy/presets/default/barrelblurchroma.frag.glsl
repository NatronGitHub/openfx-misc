// https://www.shadertoy.com/view/XssGz8

// Simulates Chromatic Aberration by linearly interpolating blur-weights from red to green to blue.
// Original idea by Kusma: https://github.com/kusma/vlee/blob/master/data/postprocess.fx

// iChannel0: Source, filter=linear, wrap=clamp
// BBox: iChannel0

// To the extent possible under law,
// the author has waived all copyright and related or neighboring rights to this work.

float remap( float t, float a, float b ) {
    return clamp( (t - a) / (b - a), 0.0, 1.0 );
}
vec2 remap( vec2 t, vec2 a, vec2 b ) {
    return clamp( (t - a) / (b - a), 0.0, 1.0 );
}

//float linterp( float t ) {
//	return clamp( 1.0 - abs( 2.0*t - 1.0 ), 0.0, 1.0 );
//


//note: input [0;1]
vec3 spectrum_offset_rgb( float t )
{
    //note: optimisation from https://twitter.com/Stubbesaurus/status/818847844790575104
    //t = 3.0 * t - 0.5;
    //vec3 ret = clamp( vec3(1.0-t, 1.0-abs(t-1.0), t-1.0), 0.0, 1.0);
    float t0 = 3.0 * t - 1.5;
    vec3 ret = clamp( vec3( -t0, 1.0-abs(t0), t0), 0.0, 1.0);

    //note: old crappy code
    //vec3 ret;
    //float lo = step(t,0.5);
    //float hi = 1.0-lo;
    //float w = linterp( remap( t, 1.0/6.0, 5.0/6.0 ) );
    //ret = vec3(lo,1.0,hi) * vec3(1.0-w, w, 1.0-w);

    return ret;
    //return smoothstep( vec3(0.0), vec3(1.0), ret );
    //return pow( ret, vec3(1.0/2.2) );

}

const float gamma = 2.2;
vec3 lin2srgb( vec3 c )
{
    return pow( c, vec3(gamma) );
}
vec3 srgb2lin( vec3 c )
{
    return pow( c, vec3(1.0/gamma));
}


vec3 yCgCo2rgb(vec3 ycc)
{
    float R = ycc.x - ycc.y + ycc.z;
    float G = ycc.x + ycc.y;
    float B = ycc.x - ycc.y - ycc.z;
    return vec3(R,G,B);
}

vec3 spectrum_offset_ycgco( float t )
{
    //vec3 ygo = vec3( 1.0, 1.5*t, 0.0 ); //green-pink
    //vec3 ygo = vec3( 1.0, -1.5*t, 0.0 ); //green-purple
    vec3 ygo = vec3( 1.0, 0.0, -1.25*t ); //cyan-orange
    //vec3 ygo = vec3( 1.0, 0.0, 1.5*t ); //brownyello-blue
    return yCgCo2rgb( ygo );
}

vec3 yuv2rgb( vec3 yuv )
{
    vec3 rgb;
    rgb.r = yuv.x + yuv.z * 1.13983;
    rgb.g = yuv.x + dot( vec2(-0.39465, -0.58060), yuv.yz );
    rgb.b = yuv.x + yuv.y * 2.03211;
    return rgb;
}


// ====

//note: from https://www.shadertoy.com/view/XslGz8
vec2 radialdistort(vec2 coord, vec2 amt)
{
    vec2 cc = coord - 0.5;
    return coord + 2.0 * cc * amt;
}

// Given a vec2 in [-1,+1], generate a texture coord in [0,+1]
vec2 barrelDistortion( vec2 p, vec2 amt )
{
    p = 2.0 * p - 1.0;

    /*
     const float maxBarrelPower = 5.0;
     //note: http://glsl.heroku.com/e#3290.7 , copied from Little Grasshopper
     float theta  = atan(p.y, p.x);
     vec2 radius = vec2( length(p) );
     radius = pow(radius, 1.0 + maxBarrelPower * amt);
     p.x = radius.x * cos(theta);
     p.y = radius.y * sin(theta);

     /*/
    // much faster version
    //const float maxBarrelPower = 5.0;
    //float radius = length(p);
    float maxBarrelPower = sqrt(5.0);
    float radius = dot(p,p); //faster but doesn't match above accurately
    p *= pow(vec2(radius), maxBarrelPower * amt);
    /* */

    return p * 0.5 + 0.5;
}

//note: from https://www.shadertoy.com/view/MlSXR3
vec2 brownConradyDistortion(vec2 uv, float dist)
{
    uv = uv * 2.0 - 1.0;
    // positive values of K1 give barrel distortion, negative give pincushion
    float barrelDistortion1 = 0.1 * dist; // K1 in text books
    float barrelDistortion2 = -0.025 * dist; // K2 in text books

    float r2 = dot(uv,uv);
    uv *= 1.0 + barrelDistortion1 * r2 + barrelDistortion2 * r2 * r2;
    //uv *= 1.0 + barrelDistortion1 * r2;

    // tangential distortion (due to off center lens elements)
    // is not modeled in this function, but if it was, the terms would go here
    return uv * 0.5 + 0.5;
}

vec2 distort( vec2 uv, float t, vec2 min_distort, vec2 max_distort )
{
    vec2 dist = mix( min_distort, max_distort, t );
    //return radialdistort( uv, 2.0 * dist );
    //return barrelDistortion( uv, 1.75 * dist ); //distortion at center
    return brownConradyDistortion( uv, 75.0 * dist.x );
}

// ====

vec3 spectrum_offset_yuv( float t )
{
    //vec3 yuv = vec3( 1.0, 3.0*t, 0.0 ); //purple-green
    //vec3 yuv = vec3( 1.0, 0.0, 2.0*t ); //purple-green
    vec3 yuv = vec3( 1.0, 0.0, -1.0*t ); //cyan-orange
    //vec3 yuv = vec3( 1.0, -0.75*t, 0.0 ); //brownyello-blue
    return yuv2rgb( yuv );
}

vec3 spectrum_offset( float t )
{
    return spectrum_offset_rgb( t );
   	//return srgb2lin( spectrum_offset_rgb( t ) );
    //return lin2srgb( spectrum_offset_rgb( t ) );

    //return spectrum_offset_ycgco( t );
    //return spectrum_offset_yuv( t );
}

// ====

float nrand( vec2 n )
{
    return fract(sin(dot(n.xy, vec2(12.9898, 78.233)))* 43758.5453);
}

vec3 render( vec2 uv )
{
#define DEBUG
#if defined( DEBUG )
    if ( uv.x > 0.7 && uv.y > 0.7 )
    {
        float d = length( vec2(0.77)- uv );
        d = min( d, length( vec2(0.82)- uv ) );
        d = min( d, length( vec2(0.875)- uv ) );
        return vec3( step( d, 0.025) );
    }
#endif

    return srgb2lin(texture( iChannel0, uv ).rgb );
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 uv = fragCoord.xy/iResolution.xy;

    if ( uv.x > 0.7 &&  uv.y < 0.2 )
    {
        vec2 luv = remap( uv, vec2(0.7,0.0), vec2(1.0, 0.2) );
        vec3 c;
        c.r = step( luv.x, 1.0/3.0);
        c.g = step( 1.0/3.0, luv.x )*step(luv.x, 2.0/3.0);
        c.b = step( 2.0/3.0, luv.x );
        c *= 0.4;

        vec3 rgb = spectrum_offset_rgb( luv.x );
        c += step( abs(rgb-luv.yyy), vec3(0.0125) );

        if ( uv.y > 0.20125 )
        {
            c = spectrum_offset( luv.x );
            if ( uv.y < 0.21 )
                c = vec3(0.0);

        }

        fragColor.rgb = c;
        return;
    }

    const float MAX_DIST_PX = 50.0;
    float max_distort_px = MAX_DIST_PX * (1.0-iMouse.x/iResolution.x);
    vec2 max_distort = vec2(max_distort_px) / iResolution.xy;
    vec2 min_distort = 0.5 * max_distort;

    //vec2 oversiz = vec2(1.0);
    vec2 oversiz = distort( vec2(1.0), 1.0, min_distort, max_distort );
    uv = remap( uv, 1.0-oversiz, oversiz );

    //debug oversiz
    //vec2 distuv = distort( uv, 1.0, max_distort );
    //if ( abs(distuv.x-0.5)>0.5 || abs(distuv.y-0.5)>0.5)
    //{
    //    fragColor = vec4( 1.0, 0.0, 0.0, 1.0 ); return;
    //}

    const int num_iter = 7;
    const float stepsiz = 1.0 / (float(num_iter)-1.0);
    float rnd = nrand( uv + fract(iTime) );
    float t = rnd * stepsiz;

    vec3 sumcol = vec3(0.0);
    vec3 sumw = vec3(0.0);
    for ( int i=0; i<num_iter; ++i )
    {
        vec3 w = spectrum_offset( t );
        sumw += w;
        vec2 uvd = distort(uv, t, min_distort, max_distort ); //TODO: move out of loop
        sumcol += w * render( uvd );
        t += stepsiz;
    }
    sumcol.rgb /= sumw;
    
    vec3 outcol = sumcol.rgb;
    outcol = lin2srgb( outcol );
    outcol += rnd/255.0;
    
    fragColor = vec4( outcol, 1.0);
}
