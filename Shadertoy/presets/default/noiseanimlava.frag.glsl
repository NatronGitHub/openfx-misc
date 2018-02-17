// Taken from https://www.shadertoy.com/view/lslXRS

// Playing with different ways of animating noise. In this version, the noise is made using the ideas behind "flow noise", I'm not quite sure it qualifies though, but it looks decent enough.

// iChannel0: Rand (The output of a Rand plugin with Static Seed checked or tex12.png), filter=mipmap, wrap=repeat

//Noise animation - Lava
//by nimitz (stormoid.com) (twitter: @stormoid)


//Somewhat inspired by the concepts behind "flow noise"
//every octave of noise is modulated separately
//with displacement using a rotated vector field

//This is a more standard use of the flow noise
//unlike my normalized vector field version (https://www.shadertoy.com/view/MdlXRS)
//the noise octaves are actually displaced to create a directional flow

//Sinus ridged fbm is used for better effect.

#define time iTime*0.1

float hash21(in vec2 n){ return fract(sin(dot(n, vec2(12.9898, 4.1414))) * 43758.5453); }
mat2 makem2(in float theta){float c = cos(theta);float s = sin(theta);return mat2(c,-s,s,c);}
float noise( in vec2 x ){return texture2D(iChannel0, x*.01).x;}

vec2 gradn(vec2 p)
{
	float ep = .09;
	float gradx = noise(vec2(p.x+ep,p.y))-noise(vec2(p.x-ep,p.y));
	float grady = noise(vec2(p.x,p.y+ep))-noise(vec2(p.x,p.y-ep));
	return vec2(gradx,grady);
}

float flow(in vec2 p)
{
	float z=2.;
	float rz = 0.;
	vec2 bp = p;
	for (float i= 1.;i < 7.;i++ )
	{
		//primary flow speed
		p += time*.6;
		
		//secondary flow speed (speed of the perceived flow)
		bp += time*1.9;
		
		//displacement field (try changing time multiplier)
		vec2 gr = gradn(i*p*.34+time*1.);
		
		//rotation of the displacement field
		gr*=makem2(time*6.-(0.05*p.x+0.03*p.y)*40.);
		
		//displace the system
		p += gr*.5;
		
		//add noise octave
		rz+= (sin(noise(p)*7.)*0.5+0.5)/z;
		
		//blend factor (blending displaced system with base system)
		//you could call this advection factor (.5 being low, .95 being high)
		p = mix(bp,p,.77);
		
		//intensity scaling
		z *= 1.4;
		//octave scaling
		p *= 2.;
		bp *= 1.9;
	}
	return rz;	
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec2 p = fragCoord.xy / iResolution.xy-0.5;
	p.x *= iResolution.x/iResolution.y;
	p*= 3.;
	float rz = flow(p);
	
	vec3 col = vec3(.2,0.07,0.01)/rz;
	col=pow(col,vec3(1.4));
	fragColor = vec4(col,1.0);
}
