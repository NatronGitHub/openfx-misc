// https://www.shadertoy.com/view/XdySWm

// iChannel0: Source, filter=linear, wrap=clamp
// iChannel1: Modulate (Image containing a factor to be applied to the Shift in the first channel), filter=linear, wrap=clamp
// BBox: iChannel0

const vec2 iRenderScale = vec2(1.,1.);
const vec2 iChannelOffset[4] = vec2[4]( vec2(0.,0.), vec2(0.,0.), vec2(0.,0.), vec2(0.,0.) );
uniform float shift = 10.; // Shift (Shift in pixel units), min=-100., max=100.
uniform bool perpixel_shift = false; // Modulate (Modulate the shift by multiplying it by the first channel of the Modulate input)

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 uv = fragCoord.xy / iResolution.xy;

    float disp = shift/iResolution.x;
    if (perpixel_shift) {
        disp *= texture2D(iChannel1, (fragCoord.xy-iChannelOffset[1].xy)/iChannelResolution[1].xy).x;
    }
    vec4 left = texture(iChannel0, uv);
    vec4 right = texture(iChannel0, uv + vec2(disp, 0.0));

    vec3 color = vec3(left.r, right.gb);
    color = clamp(color, 0.0, 1.0);
    fragColor = vec4(color, 1.0);
}
