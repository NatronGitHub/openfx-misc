void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 p = vec2(fragCoord.x/iResolution.x, fragCoord.y/iResolution.y);
    fragColor = texture2D( iChannel0, p );
}
