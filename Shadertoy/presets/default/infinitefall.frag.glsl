// See: https://www.shadertoy.com/view/ltjXDW

// Conceptually along the same lines as Fabrice Neyret's infinite noise texture example, but with a few minor differences.

// iChannel0: Rand (The output of a any Rand/Noise plugin with Static Seed checked, tex12.png), filter=mipmap, wrap=repeat

uniform bool external_texture = false; // External Texture (Use an external texture)
uniform float rspeed = 1.; // Rotation Speed, min=-5., max=5.
uniform float zspeed = 2.; // Zoom Speed, min=-10., max=10.
/*
	
	Infinite zoom textures. They've been keeping me amused for years. :)

	This is a trimmed down version of something I wrote some time ago.

	Conceptually, it's the same as Fabrices cool "Infinite Fall" example, with some 
	minor differences. Obviously this version is lacking certain aesthetics	and not	
	as functional. I cheated by hardcoding them (8 in all).

	I like trimming down code, but usually set the restriction that the modifications
	don't reduce the efficiency. I also try to avoid "define"s and so forth. Easier said 
	than done. The textureless version (see below) is under two tweets, but the 
	procedural one is about a hundred characters more.

	I have some nicer examples than this one, so I might put one up at some stage. 

	Fabrice Neyret's version:
	infinite fall - short
	https://www.shadertoy.com/view/ltjXWW

    Another version I did. I put more effort into this one. Fewer shortcuts.
    Quasi Infinite Zoom Voronoi - Shane
    https://www.shadertoy.com/view/XlBXWw

 there is some significant flickering noticable with less layers.
 *(.5-abs(f-.5))*.5; //just doesnt blend too well. needs a shepperd curve
*/

///*
// Compact 2D noise function that I wrote for fun some time ago. Obviously, it's based off of 
// one of IQs functions. The aesthetics aren't the best, but not too bad all things considered. 
// It can be trimmed down even further, but it'd be difficult to do so without reducing the 
// quality, efficiency, etc.
float N(vec2 p) {
	
	vec2 f = fract(p); p-=f; f *= f*(3.-f-f);  
    return dot( mat2(fract(sin(vec4(0, 1, 57, 58) + p.x+p.y*57.)*1e4)) * vec2(1.-f.y,f.y), vec2(1.-f.x, f.x) );

}
//*/

void mainImage( out vec4 o, in vec2 p )
{
    // Screen coordinates.
    vec2 r = iResolution.xy; // Fabrice's suggestion.
	p = (p - r*.5)/r.y;

    // Variable setup, plus rotation.
	float c = 0., t = iTime, cs = cos(t*rspeed), si = sin(t*rspeed), s;
	p *= mat2(cs, -si, si, cs);
    
    
    o = vec4(0);
	
    // Hard coded to 8 layers.
	for (float i = 0.; i<8.; i++){
	
        // Fractional time component.
		s = fract((i - t*zspeed)/8.);

        if (!external_texture) {
        // Accumulating each layer.
        //
        // p*exp2(s*8.) - UV component, based on "s."
        // i*.9 - A bit of a fudge to mix the layers up a bit.
        // (.5-abs(s-.5)) - Amplituded component. Half the amplitude, to be preceise.
        o += N(p*exp2(s*8.) + i*.9) * (.5-abs(s-.5))*.5;
        } else {
        
        // Textured version. Used with the greyscale noise texture, but works with 
        // others textures too - after tweaking the UV frequency a bit. My favorite
        // is the pinkish coral texture.
        
        // Adding to "o," as suggested by Fabrice.       
        o += texture(iChannel0, p*exp2(s*8.)/2e2 + i*.9) * (.5 - abs(s-.5))*.5;        
        }
	}
}
