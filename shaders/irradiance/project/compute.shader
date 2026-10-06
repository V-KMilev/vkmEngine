/**
 * Irradiance volume - project a captured probe cube to SH-L1, and judge it.
 *
 * One invocation per probe: integrate the cube's radiance against the first four
 * spherical harmonics and store the coefficients in the volume textures at this
 * probe's cell. Directions come from a Fibonacci sphere, so every sample carries
 * the same solid angle - no per-texel cubemap area weighting to get wrong.
 *
 * The same directions are read from the backface mask captured beside the
 * radiance, giving the fraction of the sphere whose nearest surface faces away.
 * Past a quarter the probe is inside geometry and what it captured is the far
 * side of whatever contains it - light that would leak into the room next door.
 * That verdict rides in sh0's alpha for the dilation on the way out
 * (dilateProbeGrid).
 *
 * Radiance-projected coefficients are stored raw; the cosine convolution that
 * turns them into irradiance happens at lookup (sampleIrradianceVolume).
 */

layout(local_size_x = 1) in;

layout(binding = PROJECT_SLOT_PROBE)    uniform samplerCube u_probe;  // the captured probe cube
// 1 where the nearest surface faces away.
layout(binding = PROJECT_SLOT_BACKFACE) uniform samplerCube u_backface;

layout(binding = 0, rgba16f) uniform writeonly image3D u_sh0;
layout(binding = 1, rgba16f) uniform writeonly image3D u_sh1;
layout(binding = 2, rgba16f) uniform writeonly image3D u_sh2;
layout(binding = 3, rgba16f) uniform writeonly image3D u_sh3;

// Probe cell this invocation writes.
uniform int u_cellX;
uniform int u_cellY;
uniform int u_cellZ;

const int   SAMPLES        = 256;
const float BACKFACE_LIMIT = 0.25;  // sphere fraction facing away that means "inside geometry"

#include "../../constants.glsl"
#include "../../sh_l1.glsl"    // SH_Y0/SH_Y1: the projection <-> evaluation contract

void main() {
    vec3 sh0 = vec3(0.0);
    vec3 sh1 = vec3(0.0);
    vec3 sh2 = vec3(0.0);
    vec3 sh3 = vec3(0.0);

    float backfacing = 0.0;

    const float w = 4.0 * PI / float(SAMPLES);  // uniform solid angle per sample

    for (int i = 0; i < SAMPLES; ++i) {
        float t   = (float(i) + 0.5) / float(SAMPLES);
        float z   = 1.0 - 2.0 * t;
        float r   = sqrt(max(0.0, 1.0 - z * z));
        float phi = GOLDEN_ANGLE * float(i);
        vec3  d   = vec3(r * cos(phi), r * sin(phi), z);

        vec3 L = texture(u_probe, d).rgb;

        sh0 += L * (SH_Y0 * w);
        sh1 += L * (SH_Y1 * d.y * w);
        sh2 += L * (SH_Y1 * d.z * w);
        sh3 += L * (SH_Y1 * d.x * w);

        backfacing += texture(u_backface, d).r;
    }

    // Equal solid angles, so the mean over the samples is the fraction of the
    // sphere - the same property that let the projection skip area weighting.
    float valid = (backfacing / float(SAMPLES)) < BACKFACE_LIMIT ? 1.0 : 0.0;

    ivec3 cell = ivec3(u_cellX, u_cellY, u_cellZ);
    imageStore(u_sh0, cell, vec4(sh0, valid));
    imageStore(u_sh1, cell, vec4(sh1, 1.0));
    imageStore(u_sh2, cell, vec4(sh2, 1.0));
    imageStore(u_sh3, cell, vec4(sh3, 1.0));
}
