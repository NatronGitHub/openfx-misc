// https://www.shadertoy.com/view/XdfGDH

// Gaussian blur by convolution with a square kernel.
// Could be faster with two passes, since the Gaussian filter is separable.

// Adapted to Natron by F. Devernay

// iChannel0: Source, filter=linear, wrap=clamp
// iChannel1: Modulate (Image containing a factor to be applied to the Blur size in the first channel), filter=linear, wrap=clamp
// BBox: iChannel0


const vec2 iRenderScale = vec2(1.,1.);
const vec2 iChannelOffset[4] = vec2[4]( vec2(0.,0.), vec2(0.,0.), vec2(0.,0.), vec2(0.,0.) );
uniform float size = 10.; // Size (Size of the filter kernel in pixel units. The standard deviation of the corresponding Gaussian is size/2.4.), min=0., max=21.
uniform bool perpixel_size = false; // Modulate (Modulate the blur size by multiplying it by the first channel of the Modulate input)

#define KSIZE_MAX 10 // 2 * max(size) + 1

float normpdf(in float x, in float sigma)
{
	return sigma <= 0. ? float(x == 0.) : 0.39894*exp(-0.5*x*x/(sigma*sigma))/sigma;
}


void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec2 uv = fragCoord.xy / iResolution.xy;
	{
		//declare stuff
		float fSize = size * iRenderScale.x;
		if (perpixel_size) {
			fSize *= texture2D(iChannel1, (fragCoord.xy-iChannelOffset[1].xy)/iChannelResolution[1].xy).x;
		}
		int kSize = int(min(int((fSize-1)/2), KSIZE_MAX));
		int mSize = kSize*2+1;
		float kernel[KSIZE_MAX*2+1];
		vec4 final_colour = vec4(0.0);
		
		//create the 1-D kernel
		float sigma = fSize / 2.4;
		float Z = 0.0;
		for (int j = 0; j <= kSize; ++j)
		{
			kernel[kSize+j] = kernel[kSize-j] = normpdf(float(j), sigma);
		}
		
		//get the normalization factor (as the gaussian has been clamped)
		for (int j = 0; j < mSize; ++j)
		{
			Z += kernel[j];
		}
		
		//read out the texels
		for (int i=-kSize; i <= kSize; ++i)
		{
			for (int j=-kSize; j <= kSize; ++j)
			{
				final_colour += kernel[kSize+j]*kernel[kSize+i]*texture2D(iChannel0, uv + vec2(float(i),float(j)) / iResolution.xy);
	
			}
		}
		
		
		fragColor = final_colour/(Z*Z);
	}
}
