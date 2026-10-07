/*
 * The baked irradiance volume: an SH-L1 probe grid over a box, for indirect
 * diffuse. u_hasIrradianceVolume is 0 without a volume. A capture
 * (GLSceneCapture) reads one only as lent by its bake, never the grid the bake
 * writes. GLIrradianceVolume::bindForShading fills it.
 */

#include "color.glsl"
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

// Where a surface reads the volume: a third of a cell off it along its geometric normal
// @p Ng, so a floor between the probe layer above it and one inside its slab reads the room
// above, not the slab's - which dilation filled from the room below too (DDGI's surface bias).
vec3 irradianceVolumeLookup(vec3 worldPos, vec3 Ng) {
    vec3 cell = u_ivSize / vec3(textureSize(u_shVolume0, 0));
    return worldPos + Ng * (0.3 * min(cell.x, min(cell.y, cell.z)));
}

// Irradiance for normal @p n: the stored coefficients are radiance, so this
// applies the cosine-lobe convolution (Ramamoorthi), plus the quadratic zonal term
// L1 predicts (Activision's ZH3, Roughton et al. 2024): along the light's dominant
// axis, grown with how directional the light is, so bounce from one side gives a
// surface's two sides the contrast L1 alone flattens.
vec3 sampleIrradianceVolume(vec3 worldPos, vec3 n) {
    vec3 uvw = clamp((worldPos - u_ivMin) / max(u_ivSize, vec3(1e-4)), 0.0, 1.0);
    vec3 sh0 = texture(u_shVolume0, uvw).rgb;
    vec3 sh1 = texture(u_shVolume1, uvw).rgb;
    vec3 sh2 = texture(u_shVolume2, uvw).rgb;
    vec3 sh3 = texture(u_shVolume3, uvw).rgb;

    vec3 E = SH_A0 * SH_Y0 * sh0 + SH_A1 * SH_Y1 * (n.y * sh1 + n.z * sh2 + n.x * sh3);

    vec3  axis  = normalize(vec3(luma(sh3), luma(sh1), luma(sh2)) + 1e-6);
    vec3  ratio = abs(sh3 * axis.x + sh1 * axis.y + sh2 * axis.z) / max(sh0, vec3(1e-6));
    vec3  zh3   = sh0 * (0.08 * ratio + 0.6 * ratio * ratio);
    float fz    = dot(axis, n);
    E += SH_A2 * zh3 * (SH_Y20 * (3.0 * fz * fz - 1.0));
    return max(E, vec3(0.0));
}
