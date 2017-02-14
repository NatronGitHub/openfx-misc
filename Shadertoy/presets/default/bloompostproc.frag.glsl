// https://www.shadertoy.com/view/Ms2Xz3

// bloom effect, using a simple box blur and mipmaps for efficient blurring.
// Dragging the mouse along the x-axis changes bloom intensity, dragging along the y-axis changes bloom threshold.

// iChannel0: Source, filter=mipmap, wrap=repeat
// BBox: iChannel0

float Threshold = 0.0+iMouse.y/iResolution.y*1.0;
float Intensity = 2.0-iMouse.x/iResolution.x*2.0;
float BlurSize = 6.0-iMouse.x/iResolution.x*6.0;

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
	vec2 uv = (fragCoord.xy/iResolution.xy)*vec2(1.0,-1.0);
    
    vec4 Color = texture2D(iChannel0, uv);
    
    vec4 Highlight = clamp(BlurColor(uv, iChannel0, BlurSize)-Threshold,0.0,1.0)*1.0/(1.0-Threshold);
        
    fragColor = 1.0-(1.0-Color)*(1.0-Highlight*Intensity); //Screen Blend Mode
}
