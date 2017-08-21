// https://www.shadertoy.com/view/4sfSz4
// Model for single scattering in a spherical cloud with density gradient.

// ---   -> based on Mikael Lemercier & Fabrice Neyret, https://www.shadertoy.com/view/4slGWM
// ---   -> noise functions from Inigo Quilez, https://www.shadertoy.com/view/XslGRr

uniform float DENS = 1.5; // Density (tau.rho at the center), min=0., max=3.
uniform float rad = 0.7; // Radius (sphere radius), min=0., max=1.

uniform float speed = 0.125; // Speed, min=0., max=1.
#define PI 3.14159

uniform vec3 skyColor = vec3(.7,.8,1.); // Sky Color
uniform vec3 sunColor = vec3(1.,.9,.7);   // Sun Color (Energy)

mat3 m = mat3( 0.00,  0.80,  0.60,
               -0.80,  0.36, -0.48,
               -0.60, -0.48,  0.64 );

float hash( float n )
{
    return fract(sin(n)*43758.5453);
}

float noise( in vec3 x )
{
    vec3 p = floor(x);
    vec3 f = fract(x);

    f = f*f*(3.0-2.0*f);

    float n = p.x + p.y*57.0 + 113.0*p.z;

    float res = mix(mix(mix( hash(n+  0.0), hash(n+  1.0),f.x),
                        mix( hash(n+ 57.0), hash(n+ 58.0),f.x),f.y),
                    mix(mix( hash(n+113.0), hash(n+114.0),f.x),
                        mix( hash(n+170.0), hash(n+171.0),f.x),f.y),f.z);
    return res;
}

float fbm( vec3 p )
{
    float f;
    f  = 0.5000*noise( p ); p = m*p*2.02;
    f += 0.2500*noise( p ); p = m*p*2.03;
    f += 0.1250*noise( p ); p = m*p*2.01;
    f += 0.0625*noise( p );
    return f*3.;
}

vec3 fbm3( vec3 p )
{
    p += iTime*speed;
    float fx = fbm(p);
    float fy = fbm(p+vec3(1345.67,0,45.67));
    float fz = fbm(p+vec3(0,4567.8,-123.4));
    return vec3(fx,fy,fz);
}

vec3 perturb3(vec3 p, float scaleX, float scaleI)
{
    scaleX *= 2.;
    return scaleI*scaleX*fbm3(p/scaleX); // usually, to be added to p
}

// Approximation of the real function, computed with Maple
vec3  sphericalTransmittanceGradient(vec2 L, float r, float h, float z)
{
    float Lx=L.x;
    float Ly=L.y;
    float Y = (r -h);
    float xmax = sqrt(2.0*r*h - h*h);
    float b = xmax;
    float a1 = (-xmax*0.7);
    if (DENS < 2.)
        a1 = 0.0;
    float a12 = a1*a1;float a13 = a12*a1;float a14 = a12*a12;
    float Lx2 = Lx*Lx;float Lx3 = Lx2*Lx;float Lx4 = Lx3*Lx; float Lx5 = Lx4*Lx;float Lx6 = Lx5*Lx;
    float Ly2 = Ly*Ly;float Ly3 = Ly2*Ly;float Ly4 = Ly2*Ly2;float Ly5 = Ly4*Ly;float Ly6 = Ly5*Ly;
    float xmax3 = xmax*xmax*xmax;
    float Y2 = Y*Y;float Y3 = Y2*Y;float Y4 = Y2*Y2;
    float r2 = r*r;float r4 = r2*r2;
    float R2 = rad*rad;
    float H2 = z*z;
    float S = sqrt(a12*Lx2+Y2*Ly2-a12+2.*a1*Lx*Y*Ly-Y2+r2);
    float c1 = S*xmax3+5.*Lx2*r2*Y2*Ly2-3.*R2*Y2*Ly2+3.*H2*Y2*Ly2-5.*Lx2*Y4*Ly2
            -2.*Lx2*Y2*r2+5.*Ly4*Y2*r2-8.*Y2*Ly2*r2+4.*Lx2*Y4*Ly4-2.*S*a13
            -21.*S*a12*Lx2*Y*Ly+12.*S*Ly3*a12*Lx2*Y+12.*S*Lx4*a12*Y*Ly
            -3.*S*Lx2*Y*Ly*r2-2.*Ly2*a14+22.*Lx4*a14-8.*Lx6*a14-20.*a14*Lx2
            -3.*a12*r2+3.*Y2*a12-4.*Y2*Ly2*a12+Ly4*Y2*a12-8.*Ly2*a14*Lx4
            +4.*Lx4*a12*Y2-7.*Y2*a12*Lx2+10.*Ly2*a14*Lx2+Ly2*a12*r2-4.*Lx4*a12*r2
            +7.*a12*Lx2*r2+6.*a14-20.*Ly3*a13*Lx3*Y-12.*Ly4*a12*Lx2*Y2+11.*Ly3*a13*Lx*Y
            -20.*Lx5*a13*Y*Ly-12.*Lx4*a12*Y2*Ly2+41.*Lx3*a13*Y*Ly+23.*Lx2*Y2*Ly2*a12
            -21.*a13*Lx*Y*Ly+4.*a1*Lx3*Y3*Ly3-7.*a1*Y3*Ly3*Lx+3.*a1*Y3*Lx*Ly
            +4.*a1*Ly5*Y3*Lx-a1*Lx3*Y3*Ly-4.*Ly2*a12*Lx2*r2-6.*S*Y3*Ly+9.*S*Y3*Ly3
            +3.*S*H2*xmax+3.*S*Y2*xmax+3.*R2*Y2-3.*R2*r2-3.*H2*Y2+3.*H2*r2+10.*Y4*Ly2
            +3.*Y2*r2+Lx2*Y4+4.*Ly6*Y4-11.*Ly4*Y4+Ly2*r4+Lx2*r4-3.*Y4-4.*S*Ly5*Y3
            +8.*S*Lx5*a13-3.*S*R2*xmax-18.*S*a13*Lx3+12.*S*a13*Lx+3.*S*R2*Y*Ly
            -6.*S*Ly2*a13*Lx+8.*S*Ly2*a13*Lx3+6.*S*a12*Y*Ly-3.*S*H2*Y*Ly+3.*S*Lx2*Y3*Ly
            +3.*S*Y*Ly*r2-4.*S*Lx2*Y3*Ly3-3.*S*Ly3*Y*a12-3.*S*Ly3*Y*r2-3.*a1*R2*Lx*Y*Ly
            +3.*a1*H2*Lx*Y*Ly+a1*Ly3*Y*Lx*r2+a1*Lx3*Y*Ly*r2;
    c1 *= (1./3.)*DENS/(S*R2);
    float c2 = Y2*S-4.*Ly4*Y2*Lx*S+2.*Ly3*Y*S*a1-4.*Ly2*a12*Lx3*S
            +3.*Ly2*a12*Lx*S-8.*Lx4*a1*Y*Ly*S+14.*Lx2*Y*Ly*S*a1-3.*a13
            -4.*Y*Ly*S*a1-Ly2*S*Lx*r2-4.*Lx3*Y2*Ly2*S+7.*Y2*Ly2*Lx*S
            +9.*Lx3*a12*S+R2*Lx*S-2.*Y2*Lx*S-Lx3*S*r2+Lx*S*r2-H2*Lx*S
            -6.*a12*Lx*S-4.*Lx5*a12*S+Lx3*S*Y2-R2*S+a12*S-8.*Ly3*a1*Lx2*Y*S
            +12.*Ly3*a12*Lx3*Y+12.*Ly4*a1*Lx2*Y2-7.*Ly3*a12*Lx*Y+12.*Lx5*a12*Y*Ly
            +12.*Lx4*a1*Y2*Ly2-25.*Lx3*a12*Y*Ly-23.*Lx2*Y2*Ly2*a1+13.*a12*Lx*Y*Ly
            -R2*Lx*Y*Ly+H2*Lx*Y*Ly+5.*Y2*Ly2*a1-2.*Ly4*Y2*a1+4.*Ly2*a13*Lx4
            -3.*Lx4*a1*Y2+4.*Lx3*Y3*Ly3+6.*Y2*a1*Lx2-9.*Y3*Ly3*Lx
            +5.*Y3*Lx*Ly-R2*a1*Lx2+H2*a1*Lx2-5.*Ly2*a13*Lx2+4.*Ly5*Y3*Lx-3.*Lx3*Y3*Ly
            -Ly2*a1*r2+3.*Lx4*a1*r2-5.*a1*Lx2*r2+2.*a1*r2-11.*Lx4*a13+4.*Lx6*a13
            +10.*a13*Lx2+H2*S+R2*a1-3.*Y2*a1-H2*a1+Ly2*a13+3.*Ly2*a1*Lx2*r2
            +3.*Ly3*Y*Lx*r2+3.*Lx3*Y*Ly*r2-4.*Lx*Y*Ly*r2;
    c2 *= DENS/(R2*S);
    if (abs(c2) < 0.1)
        c2 = 0.1; // arbitraire
    float EX1 = exp(c1-c2*xmax);
    float EX2 = exp(c1+c2*xmax);
    float res = -2.*EX1+EX1*c2*c2*R2-EX1*c2*c2*Y2-EX1*c2*c2*H2
            -2.*EX1*c2*xmax-EX1*xmax*xmax*c2*c2+2.*EX2-EX2*c2*c2*R2+EX2*c2*c2*Y2+EX2*c2*c2*H2
            -2.*EX2*c2*xmax+EX2*xmax*xmax*c2*c2;
    res *= -DENS/(rad*rad*c2*c2*c2);
    return vec3(res);
}


float computeMeanDensRay(float y, float z, float r)
{
    float xmax = sqrt(abs(r*r - y*y));
    return DENS*(-(2./3.)*pow(xmax,3.)/(rad*rad)+2.*xmax-(2.*(y*y+z*z))*xmax/(rad*rad));
}

// Projection of the 3D problem into a 2D geometry
vec3 computeEnlighting( in vec3 cameraPos, in vec3 cameraDir, in vec3 lightDir ) {

    cameraDir += perturb3(cameraDir,.06,1.5);
    // position of I : point at the surface of the sphere
    float a = dot(cameraDir,cameraDir);
    float b = 2.0*dot(cameraDir,cameraPos);
    float c = dot(cameraPos,cameraPos) - rad*rad;
    float delta = b*b-4.0*a*c;

    if (delta <= 0.0)
        return skyColor;

    float d1 = (-b + sqrt(delta))/(2.0*a);
    float d2 = (-b - sqrt(delta))/(2.0*a);

    vec3 posI = cameraPos + d1 * cameraDir;
    vec3 posIprim = cameraPos + d2 * cameraDir;
    float d3 = length(posI-posIprim); // length of the path without scattering

    // normal of the plane containing the camera & the light
    vec3 n = cross(-lightDir,-cameraDir);
    n = normalize(n);

    float d = dot(posI,n); // distance plane - center of the sphere
    vec3 C = n*d; // center of the circle
    float r = clamp(length(posI-C),0.001,rad-0.001); // radius of the circle

    float theta = acos(clamp(dot(normalize(cameraDir),normalize(C-posI)),-1.,1.));
    float y = r*sin(theta);

    // projection of lightDir
    float IPS = acos(clamp(dot(normalize(-cameraDir),normalize(lightDir)),-1.,1.));

    vec2 L = vec2(-cos(IPS),sin(IPS));

    // check the orientation
    if (dot(cross(cameraDir,-lightDir),cross(cameraDir,normalize(posI-C))) > 0.0) {
        L.y = -L.y;
    }

    // rayleigh diffusion function
    float rayleigh = cos(IPS)*cos(IPS)+1.0;

    vec3 transmittance = sphericalTransmittanceGradient(L, r, r-y,length(C))*rayleigh;
    transmittance *= sunColor;
    transmittance += exp(-computeMeanDensRay(y, length(C), r))*skyColor;
    return transmittance;
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    //camera
    vec3 cameraPos = vec3(0.0,0.0,2.0);
    vec3 cameraTarget = vec3(0.0, 0.0, 0.0);
    vec3 ww = normalize( cameraPos - cameraTarget );
    vec3 uu = normalize(cross( vec3(0.0,1.0,0.0), ww ));
    vec3 vv = normalize(cross(ww,uu));
    vec2 q = gl_FragCoord.xy / iResolution.xy;
    vec2 p = -1.0 + 1.5*q;
    p.x *= iResolution.x/ iResolution.y;
    vec3 cameraDir = normalize( p.x*uu + p.y*vv - 1.5*ww );

    //light
    vec2 iM = 2.0 * iResolution.xy / 3.0;
    if (iM == vec2(0.)) {
        iM = vec2(iResolution.x*0.7, iResolution.y*0.7); // Default
    }

    float theta = (iM.x / iResolution.x *2.0 - 1.)*PI;
    float phi = (iM.y / iResolution.y - .5)*PI;
    vec3 lightDir =vec3(sin(theta)*cos(phi),sin(phi),-cos(theta)*cos(phi));

    vec3 enlighting = computeEnlighting( cameraPos, cameraDir, lightDir );
    fragColor = vec4(enlighting,1.0);

}