// https://www.shadertoy.com/view/Ms2Xz3

// bloom effect, using a simple box blur and mipmaps for efficient blurring.
// Dragging the mouse along the x-axis changes bloom intensity, dragging along the y-axis changes bloom threshold.

// Adapted to Natron by F. Devernay

// iChannel0: Source, filter=mipmap, wrap=clamp
// BBox: iChannel0

const vec2 iRenderScale = vec2(1.,1.);
uniform float Threshold = 0.5; // Threshold (Bloom threshold), min=0., max=1.
uniform float Intensity = 1.; // Intensity (Bloom intensity), min=0., max=2.
uniform float BlurSize = 8.; // Blur Size, min=0., max=64.

vec4 BlurColor (in vec2 Coord, in sampler2D Tex, in float MipBias)
{
	vec2 TexelSize = MipBias/iChannelResolution[0].xy;
    
    vec4  Color = texture2D(Tex, Coord, MipBias);
    Color += texture2D(Tex, Coord + vec2(TexelSize.x,0.0), MipBias);    	
    Color += texture2D(Tex, Coord + vec2(-TexelSize.x,0.0), MipBias);    	
    Color += texture2D(Tex, Coord + vec2(0.0,TexelSize.y), MipBias);    	
    Color += texture2D(Tex, Coord + vec2(0.0,-TexelSize.y), MipBias);    	
    Color += texture2D(Tex, Coord + vec2(TexelSize.x,TexelSize.y), MipBias);    	
    Color += texture2D(Tex, Coord + vec2(-TexelSize.x,TexelSize.y), MipBias);    	
    Color += texture2D(Tex, Coord + vec2(TexelSize.x,-TexelSize.y), MipBias);    	
    Color += texture2D(Tex, Coord + vec2(-TexelSize.x,-TexelSize.y), MipBias);    

    return Color/9.0;
}


void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec2 uv = fragCoord.xy/iResolution.xy;
    
    vec4 Color = texture2D(iChannel0, uv);
    
    vec4 Highlight = clamp(BlurColor(uv, iChannel0, log2(BlurSize*iRenderScale.x))-Threshold,0.0,1.0)*1.0/(1.0-Threshold);
        
    fragColor = 1.0-(1.0-Color)*(1.0-Highlight*Intensity); //Screen Blend Mode
}
