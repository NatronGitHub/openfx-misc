float rand(vec2 co)
{
  return fract(sin(dot(co.xy, vec2(12.9898,78.233))) * 43758.5453);
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
  vec2 uv = fract(iTime) + fragCoord.xy * 1e-1;
  float r = rand(vec2(1.0*uv.x, 0.8*uv.y));
  float g = rand(vec2(1.1*uv.x, 0.9*uv.y));
  float b = rand(vec2(1.2*uv.x, 1.0*uv.y));
  fragColor = vec4(r, g, b, 1.0);
}

