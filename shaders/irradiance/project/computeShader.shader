/**
 * Irradiance volume - project a captured probe cube to SH-L1.
 *
 * One invocation per probe: integrate the cube's radiance against the first four
 * spherical harmonics and store the coefficients in the volume textures at this
 * probe's cell. Directions come from a Fibonacci sphere, so every sample carries
 * the same solid angle - no per-texel cubemap area weighting to get wrong.
 *
 * Radiance-projected coefficients are stored raw; the cosine convolution that
 * turns them into irradiance happens at lookup time in the forward pass.
 */

layout(local_size_x = 1) in;

layout(binding = 0) uniform samplerCube u_probe;  // the captured probe cube

layout(binding = 0, rgba16f) uniform writeonly image3D u_sh0;
layout(binding = 1, rgba16f) uniform writeonly image3D u_sh1;
layout(binding = 2, rgba16f) uniform writeonly image3D u_sh2;
layout(binding = 3, rgba16f) uniform writeonly image3D u_sh3;

// Probe cell this invocation writes (three ints: the shader API has no ivec3 setter).
uniform int u_cellX;
uniform int u_cellY;
uniform int u_cellZ;

const int   SAMPLES      = 256;
#include "../../_common/constants.glsl"
#include "../../_common/sh_l1.glsl"    // SH_Y0/SH_Y1: the projection <-> evaluation contract

void main() {
    vec3 sh0 = vec3(0.0);
    vec3 sh1 = vec3(0.0);
    vec3 sh2 = vec3(0.0);
    vec3 sh3 = vec3(0.0);

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
    }

    ivec3 cell = ivec3(u_cellX, u_cellY, u_cellZ);
    imageStore(u_sh0, cell, vec4(sh0, 1.0));
    imageStore(u_sh1, cell, vec4(sh1, 1.0));
    imageStore(u_sh2, cell, vec4(sh2, 1.0));
    imageStore(u_sh3, cell, vec4(sh3, 1.0));
}
