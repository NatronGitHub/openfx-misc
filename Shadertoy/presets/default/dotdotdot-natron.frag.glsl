//See: https://www.shadertoy.com/view/ltl3DX

// ----------------------------------------------------------------------------------------
//	"DotDotDot" by Antoine Clappier - March 2015
//
//	Licensed under a Creative Commons Attribution-ShareAlike 4.0 International License
//	http://creativecommons.org/licenses/by-sa/4.0/
// ----------------------------------------------------------------------------------------

// An exercise in seamless loop and smooth animation. Proved to be more difficult than expected!
// The right solution was to use an exponential scaling that provides a nice acceleration 
// while maintaining the same tangents at the beginning and end of the loop.

const float OversamplingSqrt	= 4.0;
const float ShutterSpeed 		= 1.0/60.0;

const float LoopS	= 3.0;
const float Radius	= 0.53;
const float Scale0	= 0.6;
const float ExpF	= 4.05;
const float Spower	= 9.0;


const float AaCount = OversamplingSqrt*OversamplingSqrt;
   
    
vec3 Render(vec2 pQ, float pT)
{
	float T = fract(pT / LoopS);

	// Transform:
	float Scale = Scale0*exp(ExpF*T);

	float Sgn = sign(fract(0.5*pT/LoopS) - 0.5);
	float Cs = cos(Sgn*T);
	float Sn = sin(Sgn*T);

	vec2 Qt = (1.0 + Scale*pQ*mat2(Cs, -Sn, Sn, Cs)) / 2.0;

	// Dot grid:
	vec2 Gf = 2.0*fract(Qt) - 1.0;
	float D =  step(length(Gf), Radius + 0.6*pow(T, Spower));

	// Disk clip:
	D *= step(length(Qt-0.5), 0.5*Radius*exp(ExpF));

	return vec3(D);
}




void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	float Ratio = 0.5*min(iResolution.x, iResolution.y);
#ifdef FAST
	vec2 UV = (fragCoord.xy - iResolution.xy/2.0) / Ratio;	
	// Render scene in linear space
	fragColor = vec4(Render(UV, iTime), 1.0);
#else
	// Render scene in linear space with motion blur and Antialising:
	vec3 ColorSum = vec3(0.0);
	for(float F=0.0; F<AaCount; F++)
	{
		// Antialiasing:
		vec2 Off = vec2(1.0 + floor(F/OversamplingSqrt), mod(1.0+F, OversamplingSqrt)) / OversamplingSqrt;
		vec2 UV = (fragCoord.xy + Off - iResolution.xy/2.0) / Ratio;	
		
		// Motion blur:
		float t = iTime + F*ShutterSpeed / AaCount;
		
		// Render:
		ColorSum += Render(UV, t);
	}
	
	ColorSum /= AaCount;
	
	// Apply gamma:
	//ColorSum = pow(ColorSum, vec3(1.0/2.2));
	
	fragColor = vec4(ColorSum, 1.0);
#endif
}
