// iChannel0: Source, filter=mipmap, wrap=mirror

float time=iTime*0.1;
const float pi = 3.14159265;
const float sin_4000 = 0.6427876097;
const float cos_4000 = 0.7660444431;
const float sin_6000 = -0.8660254038;
const float cos_6000 = -0.5;

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
  vec2 vTextureCoord = fragCoord.xy / iResolution.xy;
  float s = sin(time);
  float c = cos(time);
  float s2 = 2.0*s*c;
  float c2 = 1.0-2.0*s*s;
  float s3 = s2*c + c2*s;
  float c3 = c2*c - s2*s;
  float ss = s2*cos_4000 + c2*sin_4000;
  float cc = c3*cos_6000 - s3*sin_6000;
  vec2 offset2   = vec2(6.0*sin(time*1.1), 3.0*cos(time*1.1));
  vec2 oldPos = vTextureCoord.xy - vec2(0.5, 0.5);
  vec2 newPos = vec2(oldPos.x * c2 - oldPos.y * s2,
	             oldPos.y * c2 + oldPos.x * s2);
  newPos = newPos*(1.0+0.2*s3) - offset2;
  vec2 temp = newPos;
  float beta = sin(temp.y*2.0 + time*8.0);
  newPos.x = temp.x + 0.4*beta;
  newPos.y = temp.y - 0.4*beta;
  fragColor = texture2D(iChannel0, newPos);
}

