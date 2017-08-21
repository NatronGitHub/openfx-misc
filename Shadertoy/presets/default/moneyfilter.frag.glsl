// https://www.shadertoy.com/view/XlsXDN

// Money filter by Giacomo Preciado
// Based on: "Free Engraved Illustration Effect Action for Photoshop" - http://snip.ly/j0gq
// e-mail: giacomo@kyrie.pe
// website: http://kyrie.pe

// iChannel0: Source, filter=linear, wrap=clamp
// BBox: iChannel0

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 xy = fragCoord.xy / iResolution.yy;
    
    float amplitud = 0.03;
    float frecuencia = 10.0;
    float gris = 1.0;
    float divisor = 4.8 / iResolution.y;
    float grosorInicial = divisor * 0.2;
    
    const int kNumPatrones = 6;
    
    // x: seno del angulo, y: coseno del angulo, z: factor de suavizado
	vec3 datosPatron[kNumPatrones];
    datosPatron[0] = vec3(-0.7071, 0.7071, 3.0); // -45
    datosPatron[1] = vec3(0.0, 1.0, 0.6); // 0
    datosPatron[2] = vec3(0.0, 1.0, 0.5); // 0
    datosPatron[3] = vec3(1.0, 0.0, 0.4); // 90
    datosPatron[4] = vec3(1.0, 0.0, 0.3); // 90
    datosPatron[5] = vec3(0.0, 1.0, 0.2); // 0

    vec4 color = texture(iChannel0, vec2(fragCoord.x / iResolution.x, xy.y));
    fragColor = color;
    
    for(int i = 0; i < kNumPatrones; i++)
    {
        float coseno = datosPatron[i].x;
        float seno = datosPatron[i].y;
        
        // RotaciÃ³n del patrÃ³n
        vec2 punto = vec2(
            xy.x * coseno - xy.y * seno,
            xy.x * seno + xy.y * coseno
        );

        float grosor = grosorInicial * float(i + 1);
        float dist = mod(punto.y + grosor * 0.5 - sin(punto.x * frecuencia) * amplitud, divisor);
        float brillo = 0.3 * color.r + 0.4 * color.g + 0.3 * color.b;

        if(dist < grosor && brillo < 0.75 - 0.12 * float(i))
        {
            // Suavizado
            float k = datosPatron[i].z;
            float x = (grosor - dist) / grosor;
            float fx = abs((x - 0.5) / k) - (0.5 - k) / k; 
            gris = min(fx, gris);
        }
    }
    
    
    float mx = iMouse.x;
    if(iMouse.z < 1.0) mx = iResolution.x * 0.5;
    if(fragCoord.x < mx) fragColor = vec4(gris, gris, gris, 1.0);
}
