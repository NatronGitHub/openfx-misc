// https://www.shadertoy.com/view/XtVGD1

// hand drawing effect
// ...i used to draw a lot, now i let computers pursue my hobbies ;)

// iChannel0: Source, filter=linear, wrap=clamp
// iChannel1: Rand (The output of a Rand plugin with Static Seed checked, or tex11.png), filter=linear, wrap=repeat
// BBox: iChannel0

// created by florian berger (flockaroo) - 2016
// License Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License.

// trying to resemle some hand drawing style


#define SHADERTOY
#ifdef SHADERTOY
#define Res0 iChannelResolution[0].xy
#define Res1 iChannelResolution[1].xy
#else
#define Res0 textureSize(iChannel0,0)
#define Res1 textureSize(iChannel1,0)
#define iResolution Res0
#endif

#define Res  iResolution.xy

#define randSamp iChannel1
#define colorSamp iChannel0


vec4 getRand(vec2 pos)
{
    return texture2D(iChannel1,pos/Res1/iResolution.y*1080.);
}

vec4 getCol(vec2 pos)
{
    // take aspect ratio into account
    vec2 uv=((pos-Res.xy*.5)/Res.y*Res0.y)/Res0.xy+.5;
    vec4 c1=texture2D(iChannel0,uv);
    vec4 e=smoothstep(vec4(-0.05),vec4(-0.0),vec4(uv,vec2(1)-uv));
    c1=mix(vec4(1,1,1,0),c1,e.x*e.y*e.z*e.w);
    float d=clamp(dot(c1.xyz,vec3(-.5,1.,-.5)),0.0,1.0);
    vec4 c2=vec4(.7);
    return min(mix(c1,c2,1.8*d),.7);
}

vec4 getColHT(vec2 pos)
{
 	return smoothstep(.95,1.05,getCol(pos)*.8+.2+getRand(pos*.7));
}

float getVal(vec2 pos)
{
    vec4 c=getCol(pos);
 	return pow(dot(c.xyz,vec3(.333)),1.)*1.;
}

vec2 getGrad(vec2 pos, float eps)
{
   	vec2 d=vec2(eps,0);
    return vec2(
        getVal(pos+d.xy)-getVal(pos-d.xy),
        getVal(pos+d.yx)-getVal(pos-d.yx)
    )/eps/2.;
}

#define AngleNum 3

#define SampNum 16
#define PI2 6.28318530717959

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 pos = fragCoord+4.0*sin(iTime*1.*vec2(1,1.7))*iResolution.y/400.;
    vec3 col = vec3(0);
    vec3 col2 = vec3(0);
    float sum=0.;
    for(int i=0;i<AngleNum;i++)
    {
        float ang=PI2/float(AngleNum)*(float(i)+.8);
        vec2 v=vec2(cos(ang),sin(ang));
        for(int j=0;j<SampNum;j++)
        {
            vec2 dpos  = v.yx*vec2(1,-1)*float(j)*iResolution.y/400.;
            vec2 dpos2 = v.xy*float(j*j)/float(SampNum)*.5*iResolution.y/400.;
	        vec2 g;
            float fact;
            float fact2;

            for(float s=-1.;s<=1.;s+=2.)
            {
                vec2 pos2=pos+s*dpos+dpos2;
                vec2 pos3=pos+(s*dpos+dpos2).yx*vec2(1,-1)*2.;
            	g=getGrad(pos2,.4);
            	fact=dot(g,v)-.5*abs(dot(g,v.yx*vec2(1,-1)))/**(1.-getVal(pos2))*/;
            	fact2=dot(normalize(g+vec2(.0001)),v.yx*vec2(1,-1));
                
                fact=clamp(fact,0.,.05);
                fact2=abs(fact2);
                
                fact*=1.-float(j)/float(SampNum);
            	col += fact;
            	col2 += fact2*getColHT(pos3).xyz;
            	sum+=fact2;
            }
        }
    }
    col/=float(SampNum*AngleNum)*.75/sqrt(iResolution.y);
    col2/=sum;
    col.x*=(.6+.8*getRand(pos*.7).x);
    col.x=1.-col.x;
    col.x*=col.x*col.x;

    vec2 s=sin(pos.xy*.1/sqrt(iResolution.y/400.));
    vec3 karo=vec3(1);
    karo-=.5*vec3(.25,.1,.1)*dot(exp(-s*s*80.),vec2(1));
    float r=length(pos-iResolution.xy*.5)/iResolution.x;
    float vign=1.-r*r*r;
	fragColor = vec4(vec3(col.x*col2*karo*vign),1);
    //fragColor=getCol(fragCoord);
}
