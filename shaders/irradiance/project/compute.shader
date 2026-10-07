/**
 * Irradiance volume - project a captured probe cube to SH-L1, and judge it.
 *
 * One workgroup per probe: integrate the cube's radiance against the first four
 * spherical harmonics and store the coefficients in the volume textures at this
 * probe's cell. Every texel of the cube is read, weighted by the solid angle it
 * covers, so a small bright patch - a sunlit spot, a lamp - reaches every probe
 * that sees it.
 *
 * The same texels are read from the backface mask captured beside the
 * radiance, giving the fraction of the sphere whose nearest surface faces away.
 * Past a quarter the probe is inside geometry and what it captured is the far
 * side of whatever contains it - light that would leak into the room next door.
 * That verdict rides in sh0's alpha for the dilation on the way out
 * (dilateProbeGrid).
 *
 * Radiance-projected coefficients are stored raw; the cosine convolution that
 * turns them into irradiance happens at lookup (sampleIrradianceVolume).
 */

// A workgroup a probe: each invocation sums whole rows of texels, then the group adds them up.
layout(local_size_x = 256) in;

layout(binding = PROJECT_SLOT_PROBE)    uniform samplerCube u_probe;  // the captured probe cube
// 1 where the nearest surface faces away.
layout(binding = PROJECT_SLOT_BACKFACE) uniform samplerCube u_backface;

layout(binding = 0, rgba16f) uniform writeonly image3D u_sh0;
layout(binding = 1, rgba16f) uniform writeonly image3D u_sh1;
layout(binding = 2, rgba16f) uniform writeonly image3D u_sh2;
layout(binding = 3, rgba16f) uniform writeonly image3D u_sh3;

// Probe cell this workgroup writes.
uniform int u_cellX;
uniform int u_cellY;
uniform int u_cellZ;

const float BACKFACE_LIMIT = 0.25;  // sphere fraction facing away that means "inside geometry"
const uint  GROUP          = 256u;

#include "../../constants.glsl"
#include "../../sh_l1.glsl"    // SH_Y0/SH_Y1: the projection <-> evaluation contract

// The solid angle a face's square from the centre to (x, y) subtends, in face units of -1..1.
float cornerArea(float x, float y) {
    return atan(x * y, sqrt(x * x + y * y + 1.0));
}

// The direction through face @p face at (u, v), each in -1..1. Which way a face's u and v run
// does not matter: every texel centre of the face is visited either way.
vec3 faceDirection(int face, float u, float v) {
    if (face == 0) return vec3( 1.0,   -v,   -u);
    if (face == 1) return vec3(-1.0,   -v,    u);
    if (face == 2) return vec3(   u,  1.0,    v);
    if (face == 3) return vec3(   u, -1.0,   -v);
    if (face == 4) return vec3(   u,   -v,  1.0);
    return vec3(-u, -v, -1.0);
}

shared vec3 s_sh0[GROUP];
shared vec3 s_sh1[GROUP];
shared vec3 s_sh2[GROUP];
shared vec3 s_sh3[GROUP];
shared vec2 s_back[GROUP];  // x = solid angle facing away, y = solid angle read

void main() {
    const uint tid  = gl_LocalInvocationID.x;
    const int  size = textureSize(u_probe, 0).x;  // its faces are square
    const float step = 2.0 / float(size);

    vec3 sh0 = vec3(0.0);
    vec3 sh1 = vec3(0.0);
    vec3 sh2 = vec3(0.0);
    vec3 sh3 = vec3(0.0);
    vec2 back = vec2(0.0);

    for (int row = int(tid); row < 6 * size; row += int(GROUP)) {
        const int   face = row / size;
        const float y0   = -1.0 + step * float(row % size);
        const float y1   = y0 + step;
        for (int i = 0; i < size; ++i) {
            float x0 = -1.0 + step * float(i);
            float x1 = x0 + step;
            // The texel's solid angle, exactly: four corner areas.
            float w = cornerArea(x0, y0) - cornerArea(x0, y1) - cornerArea(x1, y0) + cornerArea(x1, y1);
            vec3  d = normalize(faceDirection(face, x0 + 0.5 * step, y0 + 0.5 * step));

            vec3 L = textureLod(u_probe, d, 0.0).rgb;
            sh0 += L * (SH_Y0 * w);
            sh1 += L * (SH_Y1 * d.y * w);
            sh2 += L * (SH_Y1 * d.z * w);
            sh3 += L * (SH_Y1 * d.x * w);
            back += vec2(textureLod(u_backface, d, 0.0).r * w, w);
        }
    }

    s_sh0[tid] = sh0;
    s_sh1[tid] = sh1;
    s_sh2[tid] = sh2;
    s_sh3[tid] = sh3;
    s_back[tid] = back;
    barrier();
    for (uint stride = GROUP / 2u; stride > 0u; stride >>= 1u) {
        if (tid < stride) {
            s_sh0[tid]  += s_sh0[tid + stride];
            s_sh1[tid]  += s_sh1[tid + stride];
            s_sh2[tid]  += s_sh2[tid + stride];
            s_sh3[tid]  += s_sh3[tid + stride];
            s_back[tid] += s_back[tid + stride];
        }
        barrier();
    }
    if (tid != 0u) return;

    // The solid angle facing away, as a fraction of the whole sphere's.
    float valid = (s_back[0].x / s_back[0].y) < BACKFACE_LIMIT ? 1.0 : 0.0;
    sh0 = s_sh0[0];
    sh1 = s_sh1[0];
    sh2 = s_sh2[0];
    sh3 = s_sh3[0];

    ivec3 cell = ivec3(u_cellX, u_cellY, u_cellZ);
    imageStore(u_sh0, cell, vec4(sh0, valid));
    imageStore(u_sh1, cell, vec4(sh1, 1.0));
    imageStore(u_sh2, cell, vec4(sh2, 1.0));
    imageStore(u_sh3, cell, vec4(sh3, 1.0));
}
