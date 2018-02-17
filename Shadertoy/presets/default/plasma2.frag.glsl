// from: https://www.shadertoy.com/view/ldBGRR

// Just some oldskool plasma experiment

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec2 p = -1.0 + 2.0 * fragCoord.xy / iResolution.xy;

	// main code, *original shader by: 'Plasma' by Viktor Korsun (2011)
	float x = p.x;
	float y = p.y;
	float mov0 = x+y+cos(sin(iTime)*2.0)*100.+sin(x/100.)*1000.;
	float mov1 = y / 0.9 +  iTime;
	float mov2 = x / 0.2;
	float c1 = abs(sin(mov1+iTime)/2.+mov2/2.-mov1-mov2+iTime);
	float c2 = abs(sin(c1+sin(mov0/1000.+iTime)+sin(y/40.+iTime)+sin((x+y)/100.)*3.));
	float c3 = abs(sin(c2+cos(mov1+mov2+c2)+cos(mov2)+sin(x/1000.)));
	fragColor = vec4(c1,c2,c3,1.0);
}
