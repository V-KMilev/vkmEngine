/*
 * The baked irradiance volume: an SH-L1 probe grid over a box, for indirect
 * diffuse. u_hasIrradianceVolume is 0 without a volume and in a capture
 * (GLSceneCapture), so a bake never samples what it produces.
 * GLPass::bindAmbient fills it.
 */

#include "constants.glsl"
#include "sh_l1.glsl"  // SH_Y*/SH_A*: the projection <-> evaluation contract

layout(binding = IRRADIANCE_SLOT_SH0) uniform sampler3D u_shVolume0;
layout(binding = IRRADIANCE_SLOT_SH1) uniform sampler3D u_shVolume1;
layout(binding = IRRADIANCE_SLOT_SH2) uniform sampler3D u_shVolume2;
layout(binding = IRRADIANCE_SLOT_SH3) uniform sampler3D u_shVolume3;
uniform int   u_hasIrradianceVolume;
uniform vec3  u_ivMin;        // volume box min corner (world)
uniform vec3  u_ivSize;       // volume box size (world)
uniform float u_ivIntensity;
uniform float u_ivBlend;      // fade-in distance inside the box, in metres

// 0 outside the box, 1 from u_ivBlend metres inside every face, so the edge is
// no seam against the IBL. A distance, not a share per axis, so a long flat box
// fades as wide along its length as across its height.
float irradianceVolumeWeight(vec3 worldPos) {
    vec3 toMin = worldPos - u_ivMin;
    vec3 toMax = u_ivMin + u_ivSize - worldPos;
    if (any(lessThan(toMin, vec3(0.0))) || any(lessThan(toMax, vec3(0.0)))) return 0.0;
    vec3  toFace = min(toMin, toMax);                   // metres to the nearest face, per axis
    float d      = min(min(toFace.x, toFace.y), toFace.z);
    return clamp(d / max(u_ivBlend, 1e-4), 0.0, 1.0);
}

// Irradiance for normal @p n: the stored coefficients are radiance, so this
// applies the cosine-lobe convolution (Ramamoorthi).
vec3 sampleIrradianceVolume(vec3 worldPos, vec3 n) {
    vec3 uvw = clamp((worldPos - u_ivMin) / max(u_ivSize, vec3(1e-4)), 0.0, 1.0);
    vec3 sh0 = texture(u_shVolume0, uvw).rgb;
    vec3 sh1 = texture(u_shVolume1, uvw).rgb;
    vec3 sh2 = texture(u_shVolume2, uvw).rgb;
    vec3 sh3 = texture(u_shVolume3, uvw).rgb;

    vec3 E = SH_A0 * SH_Y0 * sh0 + SH_A1 * SH_Y1 * (n.y * sh1 + n.z * sh2 + n.x * sh3);
    return max(E, vec3(0.0));
}
