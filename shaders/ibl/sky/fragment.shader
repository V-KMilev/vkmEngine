/**
 * IBL bake - Rayleigh + Mie single-scattering atmosphere.
 *
 * Linear sky radiance for the cube direction. Geometry is the prelude's ATMOSPHERE_* constants,
 * coefficients Atmosphere::coefficients, both from system/sky/atmosphere.h. The sun disc and stars
 * are skybox/fragment.shader's (the stars are too fine to survive the cube's mips); night adds only
 * what lights the scene, a dim skyglow floor, so a night world is dark rather than black.
 */
in vec3 vLocalPos;

out vec4 FragColor;

uniform vec3  u_sunDir;              // direction TO the sun, normalized
uniform float u_sunIntensity;        // top-of-atmosphere sun radiance scale
uniform vec3  u_rayleighScattering;  // Atmosphere::Coefficients, per metre at sea level
uniform vec3  u_mieScattering;
uniform vec3  u_mieExtinction;
uniform float u_mieG;                // Mie phase asymmetry (forward glow)
uniform vec3  u_nightRadiance;       // skyglow the scene is lit by once the sun is down
uniform vec3  u_moonDir;             // direction TO the moon, normalized
uniform float u_moonHalo;            // radiance of the glow immediately around the moon

#include "../../constants.glsl"
#include "../../sky.glsl"

const int PRIMARY_STEPS = 16;  // along the view ray
const int LIGHT_STEPS   = 8;   // toward the sun per primary sample

// Nearest/farthest t where ray o + t*d meets a sphere of radius r at the origin.
vec2 raySphere(vec3 o, vec3 d, float r) {
    float b = dot(o, d);
    float c = dot(o, o) - r * r;
    float disc = b * b - c;
    if (disc < 0.0) return vec2(1.0, -1.0);  // miss (near > far)
    disc = sqrt(disc);
    return vec2(-b - disc, -b + disc);
}

vec3 atmosphere(vec3 dir, vec3 sunDir) {
    vec3 origin = vec3(0.0, ATMOSPHERE_PLANET_RADIUS + ATMOSPHERE_EYE_ALTITUDE, 0.0);

    vec2 t = raySphere(origin, dir, ATMOSPHERE_TOP_RADIUS);
    if (t.x > t.y) return vec3(0.0);
    t.x = max(t.x, 0.0);

    float segLen = (t.y - t.x) / float(PRIMARY_STEPS);
    float tCur   = t.x;

    vec3  sumR = vec3(0.0), sumM = vec3(0.0);
    float odR = 0.0, odM = 0.0;  // optical depth accumulated along the view ray

    for (int i = 0; i < PRIMARY_STEPS; ++i) {
        vec3  p  = origin + dir * (tCur + segLen * 0.5);
        float h  = length(p) - ATMOSPHERE_PLANET_RADIUS;
        float hr = exp(-h / ATMOSPHERE_RAYLEIGH_HEIGHT) * segLen;
        float hm = exp(-h / ATMOSPHERE_MIE_HEIGHT) * segLen;
        odR += hr;
        odM += hm;

        // Optical depth from this sample toward the sun.
        vec2  tl      = raySphere(p, sunDir, ATMOSPHERE_TOP_RADIUS);
        float segLenL = tl.y / float(LIGHT_STEPS);
        float tlCur   = 0.0;
        float odLR = 0.0, odLM = 0.0;
        bool  inShadow = false;
        for (int j = 0; j < LIGHT_STEPS; ++j) {
            vec3  pl = p + sunDir * (tlCur + segLenL * 0.5);
            float hl = length(pl) - ATMOSPHERE_PLANET_RADIUS;
            if (hl < 0.0) { inShadow = true; break; }  // the planet occludes the sun
            odLR += exp(-hl / ATMOSPHERE_RAYLEIGH_HEIGHT) * segLenL;
            odLM += exp(-hl / ATMOSPHERE_MIE_HEIGHT) * segLenL;
            tlCur += segLenL;
        }

        if (!inShadow) {
            vec3 tau = u_rayleighScattering * (odR + odLR) + u_mieExtinction * (odM + odLM);
            vec3 att = exp(-tau);
            sumR += att * hr;
            sumM += att * hm;
        }
        tCur += segLen;
    }

    float mu     = dot(dir, sunDir);
    float phaseR = 3.0 / (16.0 * PI) * (1.0 + mu * mu);
    float g      = u_mieG;
    float phaseM = 3.0 / (8.0 * PI) * ((1.0 - g * g) * (1.0 + mu * mu))
        / ((2.0 + g * g) * pow(1.0 + g * g - 2.0 * g * mu, 1.5));

    return u_sunIntensity * (sumR * u_rayleighScattering * phaseR + sumM * u_mieScattering * phaseM);
}

void main() {
    vec3 dir    = normalize(vLocalPos);
    vec3 sunDir = normalize(u_sunDir);

    // Below the horizon single scattering lights nothing: night is the flat skyglow, plus a
    // halo immediately around the moon.
    float night = skyNightFactor(sunDir);
    vec3  color = atmosphere(dir, sunDir);

    if (night > 0.0) {
        // cos^64 halves the halo ~8 degrees out, a few disc radii; cos^2 is still at half 45
        // degrees away and lights a quarter of the sky.
        float halo = pow(max(dot(dir, normalize(u_moonDir)), 0.0), 64.0);
        color += night * (u_nightRadiance + u_moonHalo * halo * vec3(0.8, 0.85, 1.0));
    }

    FragColor = vec4(color, 1.0);
}
