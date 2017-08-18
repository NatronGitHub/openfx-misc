// https://www.shadertoy.com/view/XscSWH

// iChannel0: Source, filter=linear, wrap=clamp
// BBox: iChannel0

uniform float contrast = 0.15; // Contrast, min=0., max=1.
float cellFactor = floor(clamp(iResolution.x/30., 20., 100.)/6.)*6.;
vec2 cellSize = vec2(cellFactor, cellFactor*1.5);
float light = 1. + contrast;
float dark = 1. - contrast;
float zoom = max(iResolution.x/iChannelResolution[0].x, iResolution.y/iChannelResolution[0].y);

bool tri(const vec2 p1, const vec2 p2, const vec2 p3, const vec2 p)
{
    float alpha = ((p2.y - p3.y)*(p.x - p3.x) + (p3.x - p2.x)*(p.y - p3.y)) /
        ((p2.y - p3.y)*(p1.x - p3.x) + (p3.x - p2.x)*(p1.y - p3.y));

    float beta = ((p3.y - p1.y)*(p.x - p3.x) + (p1.x - p3.x)*(p.y - p3.y)) /
       ((p2.y - p3.y)*(p1.x - p3.x) + (p3.x - p2.x)*(p1.y - p3.y));

    float gamma = 1.0 - alpha - beta;

    return (alpha >= 0.0 && beta >= 0.0 && gamma >= 0.0);
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 cell = vec2(mod(fragCoord.x, cellSize.x), mod(fragCoord.y, cellSize.y));
    vec2 norm = cell.xy / cellSize;
    vec2 res = vec2(0.0, 0.0);
    float bright = 1.0;

    //1
    if (tri(vec2(0.0,0.0), vec2(1.0,0.0), vec2(0.5,0.125), norm)) {res = vec2(0.5, -0.125); bright = light;}
    //2
    else if (tri(vec2(0.0,0.0), vec2(0.5,0.125), vec2(0.0,.375), norm)) {res = vec2(0.0, 0.375); bright = dark;}
    else if (tri(vec2(0.5,0.125), vec2(0.5,0.5), vec2(0.0,.375), norm)) {res = vec2(0.0, 0.375); bright = dark;}
    //3
    else if (tri(vec2(1.0,0.0), vec2(1.0,.375), vec2(0.5,0.125), norm)) {res = vec2(1.0, 0.375);}
    else if (tri(vec2(0.5,0.125), vec2(0.5,0.5), vec2(1.0,.375), norm)) {res = vec2(1.0, 0.375);}
    //4
    else if (tri(vec2(0.0,.375), vec2(0.0,.625), vec2(0.5,0.5), norm)) {res = vec2(0.0, 0.375); bright = light;}
    //5
    else if (tri(vec2(1.0,.375), vec2(1.0,.625), vec2(0.5,0.5), norm)) {res = vec2(1.0, 0.375); bright = light;}
    //6
    else if (tri(vec2(0.5,0.5), vec2(0.0,.625), vec2(0.0,1.0), norm)) {res = vec2(0.5, 0.875);}
    else if (tri(vec2(0.0,1.0), vec2(0.5,0.875), vec2(0.5,0.5), norm)) {res = vec2(0.5, 0.875);}
    //7
    else if (tri(vec2(0.5,0.5), vec2(1.0,.625), vec2(1.0,1.0), norm)) {res = vec2(0.5, 0.875); bright = dark;}
    else if (tri(vec2(1.0,1.0), vec2(0.5,0.875), vec2(0.5,0.5), norm)) {res = vec2(0.5, 0.875); bright = dark;}
    //8
    else if (tri(vec2(0.0,1.0), vec2(1.0,1.0), vec2(0.5,0.875), norm)) {res = vec2(0.5, 0.875); bright = light;}

    fragColor = vec4(0.0,0.0,0.0,0.0);
    for(float i = -1.5; i<1.5; i+=1.0)
    {
        for (float j = -1.5; j<1.5; j+=1.0)
        {
            fragColor += clamp(texture(
                iChannel0,
                vec2(
                    ((floor(fragCoord.x/cellSize.x)+res.x)*cellSize.x+i)/iChannelResolution[0].x,
                    ((floor(fragCoord.y/cellSize.y)+res.y)*cellSize.y+j)/iChannelResolution[0].y
                ) / zoom
            )*bright, 0.0, 1.0);
        }
    }
    fragColor = clamp(fragColor / 9.0, 0.0, 1.0);

}