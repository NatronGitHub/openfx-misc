// Taken from https://www.shadertoy.com/view/MdlXz8

// Found this on GLSL sandbox. I really liked it, changed a few things and made it tileable.
// :)
// by David Hoskins.


// Water turbulence effect by joltz0r 2013-07-04, improved 2013-07-07


// Redefine below to see the tiling...
//#define SHOW_TILING

uniform vec3 water_color = vec3(0.0, 0.35, 0.5); // Water Color
uniform vec3 light_color = vec3(1., 1., 1.); // Light Color
uniform int iter = 5; // Iterations, min=1, max=10
#define TAU 6.28318530718

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
  float time = iTime * .5 + 23.0;
  // uv should be the 0-1 uv of texture...
  vec2 uv = fragCoord.xy / iResolution.xy;

#ifdef SHOW_TILING
  vec2 p = mod(uv*TAU*2.0, TAU) - 250.0;
#else
  vec2 p = mod(uv*TAU, TAU) - 250.0;
#endif
  vec2 i = vec2(p);
  float c = 1.0;
  float inten = .005;

  for (int n = 0; n < iter; n++)
  {
    float t = time * (1.0 - (3.5 / float(n + 1)));
    i = p + vec2(cos(t - i.x) + sin(t + i.y), sin(t - i.y) + cos(t + i.x));
    c += 1.0 / length(vec2(p.x / (sin(i.x + t) / inten), p.y / (cos(i.y + t) / inten)));
  }
  c /= float(iter);
  c = 1.17 - pow(c, 1.4);
  float tmp = pow(abs(c), 8.0);
  vec3 colour = tmp * light_color;
  colour = clamp(colour + water_color, 0.0, 1.0);


#ifdef SHOW_TILING
  // Flash tile borders...
  vec2 pixel = 2.0 / iResolution.xy;
  uv *= 2.0;

  float f = floor(mod(iTime*.5, 2.0)); 	// Flash value.
  vec2 first = step(pixel, uv) * f;		   	// Rule out first screen pixels and flash.
  uv = step(fract(uv), pixel);				// Add one line of pixels per tile.
  colour = mix(colour, vec3(1.0, 1.0, 0.0), (uv.x + uv.y) * first.x * first.y); // Yellow line

#endif
  fragColor = vec4(colour, 1.0);
}
