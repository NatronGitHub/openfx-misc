const float PI=3.1415926535897932384626433832795;

// by maq/floppy
const float R=0.2;      // to play
void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec3 col;
	vec2 uv = -0.5+fragCoord.xy / iResolution.xy;
	uv.y*=0.66; // hack to get ar nice on 16:10
	vec2 p = uv;
	float d=sqrt(dot(p,p));
	float fac,fac2;
	if(d<R)
	{
		uv.x=p.x/(R+sqrt(R-d));
		uv.y=p.y/(R+sqrt(R-d));
		fac = 0.005;
		fac2 = 5.0;
	}
	else
	{
		uv.x=p.x/(d*d);
		uv.y=p.y/(d*d);
		fac = 0.02;
		fac2 = 25.0;
	}

	uv.x=uv.x-iMouse.x*fac+fac*500.0*sin(0.2*iGlobalTime);
	uv.y=uv.y-iMouse.y*fac+fac*500.0*sin(0.4*iGlobalTime);
	col = texture2D(iChannel0, uv/fac2).xyz;
	col = col*exp(-3.0*(d-R)); // some lighting
	col = col*(1.1-exp(-8.0*(abs(d-R)))); // and shading


	fragColor = vec4(col,1.0);
}
