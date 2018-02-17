// Taken from https://www.shadertoy.com/view/Mlf3WX

// Very cheap clouds and stars

// License Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License.
// Created by S.Guillitte

const mat2 m2 = mat2(.8,.6,-.6,.8);

float stars(in vec2 p){
    vec2 z = p;
    p=floor(p*5.)/5.+0.1;
	float r = 80.*dot(z-p,z-p);
    z=p;
	for( int i=0; i< 3; i++ ) 
	{		
        z=m2*z*1.1+1.3;
        z+=15.*sin(z+3.*sin(p.yx));        
	}        	
	return clamp(2.5-length(z),0.,1.)*exp(-r);
}

float noise(in vec2 p){
	
    float res=0.;
    float f=1.;
	for( int i=0; i< 2; i++ ) 
	{		
        p=m2*p*f+4.3;     
        f*=1.1;
        res+=(sin(p+sin(p.yx))).x;
	}        	
	return (res/3.);
}

float fbm(in vec2 p){
	
    float res=0.;
    float f=1.;
	for( int i=0; i< 5; i++ ) 
	{
        f*=2.;
        res+=noise(f*p)/f;
	}        	
	return res;
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec2 p = 15.*(-iResolution.xy+2.0*fragCoord.xy)/iResolution.y;
	float f = stars(p);
	p+=.8*iTime;	
	vec3 col = vec3(f*f*f,f*f*.8,f*.8);
    f= clamp(fbm(p*.1),0.,1.);
    if(f>.1)col =vec3(f*f*f,f*f,f);
    else col = mix(col,vec3(f*f*f,f*f,f),10.*f);

	fragColor = vec4(col,1.0);
}
