/**
 * GTAO denoise: visibility and bent normal averaged over a 5x5 neighbourhood on
 * the pixel's own surface, then shaped into the AO factor. Spatial only: the
 * engine has no temporal filter.
 *
 * A neighbour counts by a Gaussian of its distance and by how nearly its depth
 * lies on the pixel's plane, sloped from the flatter side of each axis so an
 * adjacent edge does not tilt it (XeGTAO's edge-aware weighting). So occlusion
 * never bleeds across a silhouette.
 */
layout(local_size_x = GROUP_IMAGE, local_size_y = GROUP_IMAGE) in;

// r = encoded visibility, gb = oct bent normal.
layout(binding = POST_SLOT_AO)       uniform sampler2D u_raw;
// Linear view depth; level 0 is full size.
layout(binding = POST_SLOT_AO_DEPTH) uniform sampler2D u_depthMips;
layout(binding = 0, rgba8) uniform writeonly image2D u_dst;

uniform float u_intensity;  // occlusion strength
uniform float u_power;      // contrast curve

#include "../../depth.glsl"
#include "../../normal_codec.glsl"
#include "../visibility.glsl"

const int   RADIUS    = 2;
const float SIGMA     = 1.5;   // pixels
// Distance off the plane, as a fraction of depth, where a neighbour stops counting.
const float TOLERANCE = 0.02;

// The group's pixels plus a RADIUS border, fetched and decoded once rather than
// by up to 25 threads each.
const int TILE = GROUP_IMAGE + 2 * RADIUS;
shared float s_depth[TILE * TILE];
shared vec4  s_aoBent[TILE * TILE];  // visibility, then the bent normal, decoded

// Gaussian weight by squared pixel distance.
float gaussian(int distanceSq) {
    return exp(-0.5 * float(distanceSq) / (SIGMA * SIGMA));
}

float tileDepth(ivec2 local) { return s_depth[local.y * TILE + local.x]; }

// Intensity scales the occluded part, power adds bite.
float shapedAO(float visibility) {
    return pow(clamp(1.0 - u_intensity * (1.0 - visibility), 0.0, 1.0), u_power);
}

void main() {
    const ivec2 size  = imageSize(u_dst);
    const ivec2 last  = size - 1;
    const ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 base  = ivec2(gl_WorkGroupID.xy) * GROUP_IMAGE - RADIUS;

    // Every thread loads before any may leave: all must reach the barrier.
    const int thread = int(gl_LocalInvocationIndex);
    for (int i = thread; i < TILE * TILE; i += GROUP_IMAGE * GROUP_IMAGE) {
        const ivec2 q = clamp(base + ivec2(i % TILE, i / TILE), ivec2(0), last);
        const vec4 raw = texelFetch(u_raw, q, 0);
        s_depth[i]  = texelFetch(u_depthMips, q, 0).r;
        s_aoBent[i] = vec4(decodeVisibility(raw.r), octDecode(raw.gb));
    }
    barrier();

    if (any(greaterThanEqual(texel, size))) return;
    const ivec2 local  = ivec2(gl_LocalInvocationID.xy) + RADIUS;
    const vec4  centre = s_aoBent[local.y * TILE + local.x];
    const float z      = tileDepth(local);
    if (z >= SKY_DEPTH) {
        imageStore(u_dst, texel, vec4(shapedAO(centre.x), octEncode(centre.yzw), 1.0));
        return;
    }

    // Slope from whichever neighbour on each axis is nearer the pixel's depth.
    const float zl   = tileDepth(local - ivec2(1, 0));
    const float zr   = tileDepth(local + ivec2(1, 0));
    const float zd   = tileDepth(local - ivec2(0, 1));
    const float zu   = tileDepth(local + ivec2(0, 1));
    const float dzdx = abs(zr - z) < abs(z - zl) ? zr - z : z - zl;
    const float dzdy = abs(zu - z) < abs(z - zd) ? zu - z : z - zd;
    const float band = max(z * TOLERANCE, 1e-4);

    float ao    = 0.0;
    vec3  bent  = vec3(0.0);
    float total = 0.0;
    for (int dy = -RADIUS; dy <= RADIUS; ++dy) {
        for (int dx = -RADIUS; dx <= RADIUS; ++dx) {
            const int   at  = (local.y + dy) * TILE + (local.x + dx);
            const float off = abs(s_depth[at] - (z + float(dx) * dzdx + float(dy) * dzdy));
            const float w   = gaussian(dx * dx + dy * dy) * clamp(1.0 - off / band, 0.0, 1.0);
            if (w <= 0.0) continue;

            // Averaged decoded: a blend of two octahedral encodings is not
            // the encoding of their blend.
            const vec4 neighbour = s_aoBent[at];
            ao    += neighbour.x * w;
            bent  += neighbour.yzw * w;
            total += w;
        }
    }

    // The centre's weight is one, so total is never zero.
    const vec3 bentN = dot(bent, bent) > 1e-8 ? normalize(bent) : centre.yzw;
    imageStore(u_dst, texel, vec4(shapedAO(ao / total), octEncode(bentN), 1.0));
}
