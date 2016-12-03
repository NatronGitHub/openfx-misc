// See: https://www.shadertoy.com/view/ltjXDW

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

*/

void mainImage( out vec4 o, in vec2 p )
{
	float t = iGlobalTime;
	float c = cos(t), s = sin(t);
	vec2 R = iResolution.xy;
	p = (p - R*.5)/R.y * mat2(c, s, -s, c);
	o = vec4(0.);
	// Hard coded to 8 layers.
	for (float i = 0.; i<8.; i++)
	{
		// Fractional time component.
		s = fract((i - t*2.)/8.);

		// Accumulating each layer.
		//
		// p*exp2(s*8.) - UV component, based on "s."
		// i*.9 - A bit of a fudge to mix the layers up a bit.
		// (.5-abs(s-.5)) - Amplituded component. Half the amplitude, to be precise.
		//o += N(p*exp2(s*8.) + i*.9) * (.5-abs(s-.5))*.5;

		// Textured version. Used with the greyscale noise texture, but works with 
		// others textures too - after tweaking the UV frequency a bit. My favorite
		// is the pinkish coral texture.

		// Adding to "o," as suggested by Fabrice.       
		o += texture2D(iChannel0, p*exp2(s*8.)/2e2 + i*.9) * (.5 - abs(s-.5))*.5;
	}
}
