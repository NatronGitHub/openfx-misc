// iChannel0: Source, filter=mipmap, wrap=mirror
//
// mousePosition is used to rotate the ball.

uniform vec2 speed = vec2(0.2, 0.4); // Speed (The ball rotation speed.)
uniform float R = 0.2; // Radius (The ball radius as a fraction of image width.) min=0, max=1

const vec2 iRenderScale = vec2(1.,1.);
const float PI=3.1415926535897932384626433832795;

// by maq/floppy
void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec3 col;
	vec2 uv = -0.5+fragCoord.xy / iResolution.xy;
	uv.y*=iResolution.y/iResolution.x; // fix ar
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

	uv.x=uv.x-(iMouse.x/ iResolution.x - 0.5)*fac*500+fac*500.0*sin(speed.x*iTime);
	uv.y=uv.y-(iMouse.y/ iResolution.x - 0.5*iResolution.y/iResolution.x)*fac*500+fac*500.0*sin(speed.y*iTime);
	col = texture2D(iChannel0, uv/fac2).xyz;
	col = col*exp(-3.0*(d-R)); // some lighting
	col = col*(1.1-exp(-8.0*(abs(d-R)))); // and shading


	fragColor = vec4(col,1.0);
}
