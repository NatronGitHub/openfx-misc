// https://www.shadertoy.com/view/MttXWl

// CRT Effect Pulled from : "[SIG15] Mario World 1-1" by Krzysztof Narkowicz @knarkowicz
//
//

// iChannel0: Source, filter=linear, wrap=clamp
// BBox: iChannel0

#define SPRITE_DEC( x, i ) 	mod( floor( i / pow( 4.0, mod( x, 8.0 ) ) ), 4.0 )
#define SPRITE_DEC2( x, i ) mod( floor( i / pow( 4.0, mod( x, 11.0 ) ) ), 4.0 )
#define RGB( r, g, b ) vec3( float( r ) / 255.0, float( g ) / 255.0, float( b ) / 255.0 )

vec2 CRTCurveUV( vec2 uv )
{
    uv = uv * 2.0 - 1.0;
    vec2 offset = abs( uv.yx ) / vec2( 6.0, 4.0 );
    uv = uv + uv * offset * offset;
    uv = uv * 0.5 + 0.5;
    return uv;
}

void DrawVignette( inout vec3 color, vec2 uv )
{
    float vignette = uv.x * uv.y * ( 1.0 - uv.x ) * ( 1.0 - uv.y );
    vignette = clamp( pow( 16.0 * vignette, 0.3 ), 0.0, 1.0 );
    color *= vignette;
}

void DrawScanline( inout vec3 color, vec2 uv )
{
    float scanline 	= clamp( 0.95 + 0.05 * cos( 3.14 * ( uv.y + 0.008 * iTime ) * 240.0 * 1.0 ), 0.0, 1.0 );
    float grille 	= 0.85 + 0.15 * clamp( 1.5 * cos( 3.14 * uv.x * 640.0 * 1.0 ), 0.0, 1.0 );
    color *= scanline * grille * 1.2;
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    // we want to see at least 224x192 (overscan) and we want multiples of pixel size
    float resMultX  = floor( iResolution.x / 224.0 );
    float resMultY  = floor( iResolution.y / 192.0 );
    float resRcp    = 1.0 / max( min( resMultX, resMultY ), 1.0 );

    float time		= iTime;
    float screenWidth	= floor( iResolution.x * resRcp );
    float screenHeight	= floor( iResolution.y * resRcp );
    float pixelX 	= floor( fragCoord.x * resRcp );
    float pixelY 	= floor( fragCoord.y * resRcp );

    vec3 color = RGB( 200, 200, 252 );

    // CRT effects (curvature, vignette, scanlines and CRT grille)
    vec2 uv    = fragCoord.xy / iResolution.xy;
    vec2 crtUV = CRTCurveUV( uv );
    if ( crtUV.x < 0.0 || crtUV.x > 1.0 || crtUV.y < 0.0 || crtUV.y > 1.0 )
    {
        color = vec3( 0.0, 0.0, 0.0 );
    }
    DrawVignette( color, crtUV );
    DrawScanline( color, uv );

    vec2 tuv = fragCoord.xy / iChannelResolution[0].xy;
    fragColor.xyz 	= color * texture(iChannel0, tuv).xyz;
    fragColor.w		= 1.0;
}