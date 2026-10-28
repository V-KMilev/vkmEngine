/**
 * Irradiance volume - light what each probe sees and project it to SH-L1, and judge the probe.
 *
 * One workgroup per probe, over its tile of the surfel atlas (shaders/irradiance/surfel.glsl):
 * six faces of texels, each the surface nearest the probe in its direction, or the sky. A surface
 * is lit as the forward pass lights it but for the view: diffuse, its bounce color folding in
 * what the specular reflects. The lights, unshadowed but the shadowed sun, which reads the
 * capture's map over the box (GLCaptureShadow); what it emits; and the environment - inside the
 * box the grid the gather before made, none on the first, so a wall is lit by what reaches it and
 * never by the sky through it; outside, the sky's irradiance. A direction meeting no surface sees
 * the sky.
 *
 * The radiance is integrated against the first four spherical harmonics, every texel weighted by
 * the solid angle it covers, so a small bright patch reaches every probe that sees it. The texels
 * where the probe sees a surface's back give the share of the sphere inside geometry: past
 * BACKFACE_LIMIT the probe is inside a wall, and what it sees is the far side - light that would
 * leak into the room next door. That verdict rides in sh0's alpha, and the fill
 * (shaders/irradiance/fill) replaces the probe by its trusted neighbours.
 *
 * Stored raw; the cosine convolution to irradiance happens at lookup (sampleIrradianceVolume).
 */

layout(local_size_x = 256) in;

#include "../../constants.glsl"
#include "../../sh_l1.glsl"  // SH_Y0/SH_Y1: the projection <-> evaluation contract
#include "../../area_light.glsl"
#include "../../shadows.glsl"
#include "../../ambient.glsl"  // the sky's irradiance and the volume, as the forward pass reads them
#include "../surfel.glsl"

layout(binding = RELIGHT_SLOT_SURFELS) uniform usampler2D u_surfels;
layout(binding = IBL_SLOT_ENV_CUBE)    uniform samplerCube u_envCube;

// The lights each surfel reads, by where it is (GLIrradianceBaker::listLights): data[0] and [1] the
// directional lights' count and where their indices begin, [2] and [3] those of the lights reaching
// out of the box, then a count and a beginning per probe cell, X fastest; then the index lists.
layout(std430, binding = SSBO_RELIGHT_LIGHTS) readonly buffer RelightLightBlock {
    uint data[];
} u_relightLights;

layout(binding = 0, rgba16f) uniform writeonly image3D u_sh0;
layout(binding = 1, rgba16f) uniform writeonly image3D u_sh1;
layout(binding = 2, rgba16f) uniform writeonly image3D u_sh2;
layout(binding = 3, rgba16f) uniform writeonly image3D u_sh3;

// The box the probes stand in, at their cells' centres, is irradiance_volume.glsl's, set with no
// fade; on the first gather u_hasIrradianceVolume is 0 and the box reads no grid.
uniform int  u_faceSize;        // texels along a tile face's edge
uniform int  u_tilesPerRow;     // probe tiles along the atlas's width, six faces each
uniform mat3 u_faceToWorld[6];  // a face's view frame: (x, y, -1) through it, in world space

const float BACKFACE_LIMIT = 0.25;  // sphere fraction facing away that means "inside geometry"
const uint  GROUP          = gl_WorkGroupSize.x;

// The solid angle a face's square from the centre to (x, y) subtends, in face units of -1..1.
float cornerArea(float x, float y) {
    return atan(x * y, sqrt(x * x + y * y + 1.0));
}

// Irradiance at @p p around @p n from light @p i, the shadowed sun through its map.
vec3 lightIrradiance(uint i, vec3 p, vec3 n) {
    Light light = u_lights.lights[i];
    int   type  = int(light.position.w);
    vec3  color = light.color.xyz * light.color.w;

    if (type == LIGHT_RECT || type == LIGHT_DISK) return color * areaDiffuse(light, type, p, n) * PI;

    vec3  L;
    float atten = punctualAttenuation(light, type, p, L);
    float ndotl = dot(n, L);
    if (atten <= 0.0 || ndotl <= 0.0) return vec3(0.0);
    if (type == LIGHT_DIRECTIONAL && int(light.spot.w) >= 0) {
        atten *= sample2DSlot(u_shadow.csmBase, p, n, ndotl);
    }
    return color * atten * ndotl;
}

// Irradiance at @p p around @p n from the lights listed from @p first, @p count of them.
vec3 listIrradiance(uint first, uint count, vec3 p, vec3 n) {
    vec3 E = vec3(0.0);
    for (uint k = 0u; k < count; ++k) E += lightIrradiance(u_relightLights.data[first + k], p, n);
    return E;
}

// Irradiance at @p p around @p n from every light that reaches it.
vec3 directIrradiance(vec3 p, vec3 n) {
    const uint suns    = uint(RELIGHT_DIRECTIONAL_HEAD);
    const uint outside = uint(RELIGHT_OUTSIDE_HEAD);
    vec3 E = listIrradiance(u_relightLights.data[suns + 1u], u_relightLights.data[suns], p, n);
    if (irradianceVolumeWeight(p) <= 0.0) {
        return E + listIrradiance(u_relightLights.data[outside + 1u], u_relightLights.data[outside], p, n);
    }
    const ivec3 grid = imageSize(u_sh0);
    const ivec3 cell = clamp(ivec3((p - u_ivMin) / u_ivSize * vec3(grid)), ivec3(0), grid - 1);
    const uint  head = uint(RELIGHT_CELL_HEADS) + 2u * uint(cell.x + grid.x * (cell.y + grid.y * cell.z));
    return E + listIrradiance(u_relightLights.data[head + 1u], u_relightLights.data[head], p, n);
}

// The environment's irradiance over PI at @p p around @p n: inside the box - judged where the
// forward pass reads the volume (irradianceVolumeLookup), so a wall on the box's face takes the
// grid as the frame lights it - the gather before's grid, else the sky's.
vec3 indirectIrradiance(vec3 p, vec3 n) {
    const vec3 lookup = irradianceVolumeLookup(p, n);
    if (irradianceVolumeWeight(lookup) > 0.0) {
        if (u_hasIrradianceVolume == 0) return vec3(0.0);
        return irradianceVolumeAt(lookup, n);
    }
    return u_hasIBL == 1 ? textureLod(u_irradiance, n, 0.0).rgb : vec3(FLAT_AMBIENT);
}

// The radiance the probe at @p probe sees along @p dir from @p texel, and whether it sees a back.
vec3 radiance(uvec4 texel, vec3 probe, vec3 dir, out bool back) {
    back = false;
    if (surfelIsSky(texel)) {
        // At the level whose texel is a surfel's, as the capture's rasterised sky read it: level 0
        // would point-sample a sun or a window a surfel covers many texels of.
        const float level = log2(float(textureSize(u_envCube, 0).x) / float(u_faceSize));
        return u_hasIBL == 1 ? textureLod(u_envCube, dir, max(level, 0.0)).rgb : vec3(0.0);
    }

    Surfel s = unpackSurfel(texel);
    if (s.distance < 0.0) {
        back = true;
        return vec3(0.0);
    }
    vec3 p = probe + dir * s.distance;
    vec3 E = directIrradiance(p, s.normal) / PI + indirectIrradiance(p, s.normal) * s.ao;
    return s.emission + s.bounce * E;
}

shared vec3 s_sh0[GROUP];
shared vec3 s_sh1[GROUP];
shared vec3 s_sh2[GROUP];
shared vec3 s_sh3[GROUP];
shared vec2 s_back[GROUP];  // x = solid angle facing away, y = solid angle read

void main() {
    const uint  tid   = gl_LocalInvocationID.x;
    const int   probe = int(gl_WorkGroupID.x);
    const ivec3 grid  = imageSize(u_sh0);
    const ivec3 cell  = ivec3(probe % grid.x, (probe / grid.x) % grid.y, probe / (grid.x * grid.y));
    // At its texel's centre, as GLIrradianceBaker places each probe it captures (probePosition).
    const vec3  at    = u_ivMin + u_ivSize * (vec3(cell) + 0.5) / vec3(grid);

    const int   size   = u_faceSize;
    const float step   = 2.0 / float(size);
    const ivec2 tile   = ivec2((probe % u_tilesPerRow) * 6 * size, (probe / u_tilesPerRow) * size);

    vec3 sh0  = vec3(0.0);
    vec3 sh1  = vec3(0.0);
    vec3 sh2  = vec3(0.0);
    vec3 sh3  = vec3(0.0);
    vec2 back = vec2(0.0);

    for (int t = int(tid); t < 6 * size * size; t += int(GROUP)) {
        const int   face = t / (size * size);
        const int   i    = t % size;
        const int   j    = (t / size) % size;
        const float x0   = -1.0 + step * float(i);
        const float y0   = -1.0 + step * float(j);
        const float x1   = x0 + step;
        const float y1   = y0 + step;
        // The texel's solid angle, exactly: four corner areas.
        float w = cornerArea(x0, y0) - cornerArea(x0, y1) - cornerArea(x1, y0) + cornerArea(x1, y1);
        vec3  d = normalize(u_faceToWorld[face] * vec3(x0 + 0.5 * step, y0 + 0.5 * step, -1.0));

        bool  isBack;
        uvec4 texel = texelFetch(u_surfels, tile + ivec2(face * size + i, j), 0);
        vec3  L     = radiance(texel, at, d, isBack);
        sh0  += L * (SH_Y0 * w);
        sh1  += L * (SH_Y1 * d.y * w);
        sh2  += L * (SH_Y1 * d.z * w);
        sh3  += L * (SH_Y1 * d.x * w);
        back += vec2(isBack ? w : 0.0, w);
    }

    s_sh0[tid]  = sh0;
    s_sh1[tid]  = sh1;
    s_sh2[tid]  = sh2;
    s_sh3[tid]  = sh3;
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
    float trusted = (s_back[0].x / s_back[0].y) < BACKFACE_LIMIT ? 1.0 : 0.0;
    imageStore(u_sh0, cell, vec4(s_sh0[0], trusted));
    imageStore(u_sh1, cell, vec4(s_sh1[0], 1.0));
    imageStore(u_sh2, cell, vec4(s_sh2[0], 1.0));
    imageStore(u_sh3, cell, vec4(s_sh3[0], 1.0));
}
