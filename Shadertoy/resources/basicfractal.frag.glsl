//Basic fractal by @paulofalcao

const int maxIterations=6;//a nice value for fullscreen is 8

float circleSize=1.0/(3.0*pow(2.0,float(maxIterations)));

//generic rotation formula
vec2 rot(vec2 uv,float a){
	return vec2(uv.x*cos(a)-uv.y*sin(a),uv.y*cos(a)+uv.x*sin(a));
}

void mainImage( out vec4 fragColor, in vec2 fragCoord ){
	//normalize stuff
	vec2 uv=iResolution.xy;uv=-.5*(uv-2.0*fragCoord.xy)/uv.x;

	//global rotation and zoom
	uv=rot(uv,iGlobalTime);
	uv*=sin(iGlobalTime)*0.5+1.5;
	
	//mirror, rotate and scale 6 times...
	float s=0.3;
	for(int i=0;i<maxIterations;i++){
		uv=abs(uv)-s;
		uv=rot(uv,iGlobalTime);
		s=s/2.1;
	}
	
	//draw a circle
	float c=length(uv)>circleSize?0.0:1.0;	

	fragColor = vec4(c,c,c,1.0);
}
