// https://www.shadertoy.com/view/4dfGDH

// And another filter!

// Adapted for Natron by F. Devernay

// iChannel0: Source, filter=nearest, wrap=clamp
// BBox: iChannel0

const vec2 iRenderScale = vec2(1.,1.);
uniform float sigma_s = 10.0; // Spatial Std Dev (Standard deviation of the spatial kernel in pixel units), min=0., max=20.
uniform float sigma_r = 0.1; // Value Std Dev (Standard deviation of the range kernel in intensity unit), min=0., max=1.
#define MSIZE 30 // should be 1.5 times the maximum value for sigma_s

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
	vec2 uv = fragCoord.xy / iResolution.xy;
	vec3 c = texture2D(iChannel0, uv).rgb;
	{
		//declare stuff
		int kSize = int(min((MSIZE-1)/2., 1.5*sigma_s*iRenderScale.x));
		float kernel[MSIZE];
		vec3 final_colour = vec3(0.0);
		
		//create the 1-D kernel
		float Z = 0.0;
		for (int j = 0; j <= kSize; ++j)
		{
			kernel[kSize+j] = kernel[kSize-j] = normpdf(float(j), sigma_s*iRenderScale.x);
		}
		
		
		vec3 cc;
		float factor;
		float bZ = 1.0/normpdf(0.0, sigma_r);
		//read out the texels
		for (int i=-kSize; i <= kSize; ++i)
		{
			for (int j=-kSize; j <= kSize; ++j)
			{
				cc = texture2D(iChannel0, uv + (vec2(float(i),float(j))) / iResolution.xy).rgb;
				factor = normpdf3(cc-c, sigma_r)*bZ*kernel[kSize+j]*kernel[kSize+i];
				Z += factor;
				final_colour += factor*cc;

			}
		}
		
		
		fragColor = vec4(final_colour/Z, 1.0);
	}
}
