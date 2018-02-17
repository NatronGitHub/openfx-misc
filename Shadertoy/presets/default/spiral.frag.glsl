// https://www.shadertoy.com/view/XdSGzR

// :V

#define PI 3.141592653589793
#define TAU 6.283185307179586

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec2 p = 2.0*(0.5 * iResolution.xy - fragCoord.xy) / iResolution.xx;
	float angle = atan(p.y, p.x);
	float turn = (angle + PI) / TAU;
	float radius = sqrt(p.x*p.x + p.y*p.y);
	
	float rotation = 0.04 * TAU * iTime;
	float turn_1 = turn + rotation;
	
	float n_sub = 2.0;
	
	float turn_sub = mod(float(n_sub) * turn_1, float(n_sub));
	
	float k_sine = 0.1 * sin(3.0 * iTime);
	float sine = k_sine * sin(50.0 * (pow(radius, 0.1) - 0.4 * iTime));
	float turn_sine = turn_sub + sine;

	int n_colors = 5;
	int i_turn = int(mod(float(n_colors) * turn_sine, float(n_colors)));
	
	int i_radius = int(1.5/pow(radius*0.5, 0.6) + 5.0 * iTime);
		
	int i_color = int(mod(float(i_turn + i_radius), float(n_colors)));
	
	vec3 color;
	if(i_color == 0) { 
		color = vec3(1.0, 1.0, 1.0);		  
	} else if(i_color == 1) {
		color = vec3(0.0, 0.0, 0.0);	
	} else if(i_color == 2) {
		color = vec3(1.0, 0.0, 0.0);	
	} else if(i_color == 3) {
		color = vec3(1.0, 0.5, 0.0);	
	} else if(i_color == 4) {
		color = vec3(1.0, 1.0, 0.0);	
	}
	
	color *= pow(radius, 0.5)*1.0;
	
	fragColor = vec4(color, 1.0);
}
