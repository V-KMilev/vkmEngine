/**
 * IBL bake - Rayleigh + Mie single-scattering atmosphere.
 *
 * Renders analytic sky radiance for the cube direction, baked into the
 * environment cubemap that feeds IBL + the skybox. Linear radiance out (the
 * composite pass owns tonemap + gamma). Single scattering only - a couple of
 * short ray-marches per texel - which is plenty for an environment bake. The
 * sharp sun disc is added analytically by the skybox pass, not here - and so are
 * the stars, which are far too fine to survive a 512 cube and its prefiltered
 * mips. What night contributes *here* is the part that lights the scene: a dim
 * skyglow floor, so a night world is dark rather than black.
 */

in vec3 vLocalPos;

out vec4 FragColor;

uniform vec3  u_sunDir;        // direction TO the sun, normalized
uniform float u_sunIntensity;  // top-of-atmosphere sun radiance scale
uniform float u_rayleigh;      // Rayleigh scattering scale
uniform float u_mie;           // Mie scattering scale
uniform float u_mieG;          // Mie phase asymmetry (forward glow)
uniform vec3  u_nightRadiance; // skyglow the scene is lit by once the sun is down
uniform vec3  u_moonDir;       // direction TO the moon, normalized
uniform float u_moonHalo;      // radiance of the glow immediately around the moon

#include "../../_common/constants.glsl"
#include "../../_common/sky.glsl"

// Earth-like atmosphere (metres). Bruneton's sea-level scattering coefficients.
const float R_PLANET = 6371000.0;
const float R_ATMOS  = 6471000.0;
const vec3  BETA_R   = vec3(5.802e-6, 13.558e-6, 33.1e-6);  // Rayleigh (per m)
const float BETA_M   = 21.0e-6;                             // Mie (per m)
const float H_R      = 8000.0;                              // Rayleigh scale height
const float H_M      = 1200.0;                              // Mie scale height

const int PRIMARY_STEPS = 16;  // along the view ray
const int LIGHT_STEPS    = 8;  // toward the sun per primary sample

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
    vec3 origin = vec3(0.0, R_PLANET + 1000.0, 0.0);  // ~1 km above the ground

    vec2 t = raySphere(origin, dir, R_ATMOS);
    if (t.x > t.y) return vec3(0.0);
    t.x = max(t.x, 0.0);

    float segLen = (t.y - t.x) / float(PRIMARY_STEPS);
    float tCur   = t.x;

    vec3  sumR = vec3(0.0), sumM = vec3(0.0);
    float odR = 0.0, odM = 0.0;  // optical depth accumulated along the view ray

    for (int i = 0; i < PRIMARY_STEPS; ++i) {
        vec3  p  = origin + dir * (tCur + segLen * 0.5);
        float h  = length(p) - R_PLANET;
        float hr = exp(-h / H_R) * segLen;
        float hm = exp(-h / H_M) * segLen;
        odR += hr;
        odM += hm;

        // Optical depth from this sample toward the sun.
        vec2  tl      = raySphere(p, sunDir, R_ATMOS);
        float segLenL = tl.y / float(LIGHT_STEPS);
        float tlCur   = 0.0;
        float odLR = 0.0, odLM = 0.0;
        bool  inShadow = false;
        for (int j = 0; j < LIGHT_STEPS; ++j) {
            vec3  pl = p + sunDir * (tlCur + segLenL * 0.5);
            float hl = length(pl) - R_PLANET;
            if (hl < 0.0) { inShadow = true; break; }  // the planet occludes the sun
            odLR += exp(-hl / H_R) * segLenL;
            odLM += exp(-hl / H_M) * segLenL;
            tlCur += segLenL;
        }

        if (!inShadow) {
            vec3 tau = BETA_R * u_rayleigh * (odR + odLR)
                     + BETA_M * u_mie * 1.1 * (odM + odLM);
            vec3 att = exp(-tau);
            sumR += att * hr;
            sumM += att * hm;
        }
        tCur += segLen;
    }

    float mu     = dot(dir, sunDir);
    float phaseR = 3.0 / (16.0 * PI) * (1.0 + mu * mu);
    float g      = u_mieG;
    float phaseM = 3.0 / (8.0 * PI) * ((1.0 - g * g) * (1.0 + mu * mu)) /
                   ((2.0 + g * g) * pow(1.0 + g * g - 2.0 * g * mu, 1.5));

    return u_sunIntensity * (sumR * BETA_R * u_rayleigh * phaseR
                           + sumM * BETA_M * u_mie * phaseM);
}

void main() {
    vec3 dir    = normalize(vLocalPos);
    vec3 sunDir = normalize(u_sunDir);

    // Single scattering with the sun below the horizon is very nearly black -
    // physically right, and useless as a lighting environment. What lights a
    // night scene is the flat skyglow; the moon only adds a halo immediately
    // around itself, so the sky is not uniformly flat.
    float night = skyNightFactor(sunDir);
    vec3  color = atmosphere(dir, sunDir);

    if (night > 0.0) {
        // cos^64 puts the halo at half brightness ~8 degrees out - a few times
        // the disc's own radius. A low power is a hemisphere, not a halo: cos^2
        // is still at half brightness 45 degrees away, which lights a quarter of
        // the sky and reads as the moon bleeding over everything.
        float halo = pow(max(dot(dir, normalize(u_moonDir)), 0.0), 64.0);
        color += night * (u_nightRadiance + u_moonHalo * halo * vec3(0.8, 0.85, 1.0));
    }

    FragColor = vec4(color, 1.0);
}
