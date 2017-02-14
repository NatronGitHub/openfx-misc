// https://www.shadertoy.com/view/4dfGDH

// And another filter!
// You can drag the active zone using the mouse.

// iChannel0: Source, filter=mipmap, wrap=repeat
// BBox: iChannel0

#ifdef GL_ES
precision mediump float;
#endif

#define SIGMA 10.0
#define BSIGMA 0.1
#define MSIZE 15

float normpdf(in float x, in float sigma)
{
	return 0.39894*exp(-0.5*x*x/(sigma*sigma))/sigma;
}

float normpdf3(in vec3 v, in float sigma)
{
	return 0.39894*exp(-0.5*dot(v,v)/(sigma*sigma))/sigma;
}


void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec3 c = texture2D(iChannel0, vec2(0.0, 1.0)-(fragCoord.xy / iResolution.xy)).rgb;
	if (fragCoord.x < iMouse.x)
	{
		fragColor = vec4(c, 1.0);
		
	} else {
		
		//declare stuff
		const int kSize = (MSIZE-1)/2;
		float kernel[MSIZE];
		vec3 final_colour = vec3(0.0);
		
		//create the 1-D kernel
		float Z = 0.0;
		for (int j = 0; j <= kSize; ++j)
		{
			kernel[kSize+j] = kernel[kSize-j] = normpdf(float(j), SIGMA);
		}
		
		
		vec3 cc;
		float factor;
		float bZ = 1.0/normpdf(0.0, BSIGMA);
		//read out the texels
		for (int i=-kSize; i <= kSize; ++i)
		{
			for (int j=-kSize; j <= kSize; ++j)
			{
				cc = texture2D(iChannel0, vec2(0.0, 1.0)-(fragCoord.xy+vec2(float(i),float(j))) / iResolution.xy).rgb;
				factor = normpdf3(cc-c, BSIGMA)*bZ*kernel[kSize+j]*kernel[kSize+i];
				Z += factor;
				final_colour += factor*cc;

			}
		}
		
		
		fragColor = vec4(final_colour/Z, 1.0);
	}
}
