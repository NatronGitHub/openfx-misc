// from: https://www.shadertoy.com/view/XtjGRD

// Hypnotic color circles.

float circle(in vec2 pos, in float t, in float mult)
{
    vec2 center = vec2(0.5, 0.5 * iResolution.y / iResolution.x);
    vec2 v = vec2(center.x + cos(t) * 0.1, center.y + sin(t) * 0.1) - pos;
    
    float dist2 = dot(v, v);
    float circles = 0.5 + cos(dist2 * mult) * 0.5;
    float darkening = smoothstep(0.0, 0.5, 1.0 - length(v) * 2.0);
    
    return circles * darkening;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 pos = fragCoord.xy / iResolution.x;
    float t = iTime * 0.25;

    float r = circle(pos, t + 0.0,  100.0 + sin(t * 0.6 + 1.0) * 100.0);
    float g = circle(pos, t + 0.5,  100.0 + sin(t * 0.6 + 2.0) * 100.0);
    float b = circle(pos, t + 1.0,  100.0 + sin(t * 0.6 + 3.0) * 100.0);
    float w = circle(pos, t + 1.5, 1000.0 + sin(t + 4.0) * 200.0) * 0.35;
    
    fragColor = vec4(r+w, g+w, b+w, 1.0);
}

