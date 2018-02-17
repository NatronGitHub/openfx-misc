// https://www.shadertoy.com/view/MdGSDG

// trying to resemble van gogh drawing style

// iChannel0: Source, filter=linear, wrap=clamp
// iChannel1: Rand (The output of a Rand plugin with Static Seed checked, or tex12.png), filter=linear, wrap=repeat
// iChannel2: Tex19, filter=linear, wrap=repeat
// BBox: iChannel0

// created by florian berger (flockaroo) - 2016
// License Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License.

// trying to resemble van gogh drawing style

#define Res  iResolution.xy
#define Res0 iChannelResolution[0].xy
#define Res1 iChannelResolution[1].xy
#define Res2 iChannelResolution[2].xy

vec4 getCol(vec2 pos)
{
    vec2 uv=pos/Res0;
    
    vec4 c1 = texture2D(iChannel0,uv);
    uv = uv*vec2(-1,-1)*0.39+0.015*vec2(sin(iTime*1.1),sin(iTime*0.271));
    vec4 c2 = vec4(0.5,0.7,1.0,1.0)*1.0*texture2D(iChannel2,uv);
    float d=clamp(dot(c1.xyz,vec3(-0.5,1.0,-0.5)),0.0,1.0);
    return mix(c1,c2,1.8*d);
}

float getVal(vec2 pos, float level)
{
    return length(getCol(pos).xyz)+0.0001*length(pos-0.5*Res0);
}
    
vec2 getGrad(vec2 pos,float delta)
{
    float l = 1.0*log2(delta);
    vec2 d=vec2(delta,0);
    return vec2(
        getVal(pos+d.xy,l)-getVal(pos-d.xy,l),
        getVal(pos+d.yx,l)-getVal(pos-d.yx,l)
    )/delta;
}

vec4 getRand(vec2 pos) 
{
    vec2 uv=pos/Res1;
    uv+=1.0*float(iFrame)*vec2(0.2,0.1)/Res1;
    
    return texture2D(iChannel1,uv);
}

vec4 getColDist(vec2 pos)
{
	return floor(0.8*getCol(pos)+1.1*getRand(1.2*pos));
    float fact = clamp(length(getGrad(pos,5.0))*20.0,0.0,1.0);
	return floor(0.8*getCol(pos)+1.1*mix(getRand(0.7*pos),getRand(1.7*pos),fact));
}

#define SampNum 16

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 pos = fragCoord/Res*Res0;
	vec2 uv = fragCoord.xy / iResolution.xy;
    vec3 col=vec3(0);
    float cnt=0.0;
    float fact=1.0;
    for(int i=0;i<1*SampNum;i++)
    {
        col+=fact*getColDist(pos).xyz;
        vec2 gr=getGrad(pos,4.0);
        pos+=0.6*normalize(mix(gr.yx*vec2(1,-1),-gr,0.2));
        fact*=0.87;
        cnt+=fact;
    }
    col/=cnt;
	fragColor = vec4(col,1.0);
}
