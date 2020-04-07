// iChannel0: B, filter=linear, wrap=clamp
// iChannel1: A, filter=linear, wrap=clamp
// BBox: iChannel0

const vec2 iRenderScale = vec2(1.,1.);
const vec2 iChannelOffset[4] = vec2[4]( vec2(0.,0.), vec2(0.,0.), vec2(0.,0.), vec2(0.,0.) );

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec4 B = texture(iChannel0, fragCoord.xy / iChannelResolution[0].xy);
    vec4 A = texture(iChannel1, (fragCoord.xy-iChannelOffset[1].xy)/iChannelResolution[1].xy);

    // AB, A if A<0 and B<0
    //fragColor = A*B;
    fragColor = mix(A*B, A, vec4(lessThan(A, vec4(0.))) * vec4(lessThan(B, vec4(0.))));
}
