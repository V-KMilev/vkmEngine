/**
 * How much of a fixed light's source each texel of a lightmap page sees past the fixed casters
 * (GLLightmapBaker), into the light's channel of the page: 1 lit, 0 in its shadow. Or, for the
 * sky, the share of a pass's directions the casters hide, off the page's sky - or off the sky the
 * reflection probes' centers see.
 *
 * Percentage-closer soft shadows (Fernando 2005) at a bake's tap counts: the casters nearer the
 * light within reach of the texel give a mean depth, that depth and the source's size give the
 * penumbra, and a disk of compares that wide gives the share lit. Each compare weighs a gather of
 * four depths bilinearly, so neither search nor filter steps from map texel to map texel. A point
 * light's six faces are drawn wider than 90 degrees, so a disk near a face's edge stays on it.
 */
layout(local_size_x = GROUP_IMAGE, local_size_y = GROUP_IMAGE) in;

#include "../../../include/basis.glsl"
#include "../../../include/constants.glsl"
#include "../../../include/depth.glsl"

/*
 * The soft-shadow arithmetic that touches no map: the disk a search or filter takes its taps
 * from, and a gather's weights.
 */

// One tap of a Vogel disk. Never turned by per-pixel noise, whose grain only a temporal filter
// hides: a disk dense enough that its 2x2 compares overlap gives a smooth edge unturned.
vec2 vogelTap(int i, int n) {
    float r     = sqrt((float(i) + 0.5) / float(n));
    float theta = float(i) * GOLDEN_ANGLE;
    return r * vec2(cos(theta), sin(theta));
}

// The bilinear weights of a gather at @p at in a map @p size texels across, in its order:
// (0,1) (1,1) (1,0) (0,0) of the footprint.
vec4 gatherWeights(vec2 at, vec2 size) {
    vec2 f = fract(at * size - 0.5);
    return vec4((1.0 - f.x) * f.y, f.x * f.y, f.x * (1.0 - f.y), (1.0 - f.x) * (1.0 - f.y));
}

layout(binding = LIGHTMAP_BAKE_SLOT_POSITION) uniform sampler2D      u_position;
layout(binding = LIGHTMAP_BAKE_SLOT_NORMAL)   uniform sampler2D      u_normal;
layout(binding = LIGHTMAP_BAKE_SLOT_DEPTH)    uniform sampler2DArray u_depth;

layout(binding = 0, rgba8) uniform image2DArray u_mask;
layout(binding = 1, r16f)  uniform image2DArray u_skyLeft;  // the sky summed, the bake's own
layout(binding = 2, r8)    uniform writeonly image2DArray u_sky;  // and as the page keeps it

// The sky each reflection probe's center has left, as the passes take their shares.
layout(std430, binding = SSBO_LIGHTMAP_PROBE_SKY) buffer ProbeSky {
    float b_probeSky[];
};

uniform int   u_page;
uniform ivec2 u_origin;         // the first texel of the rows this dispatch bakes
uniform int   u_channel;
uniform int   u_kind;           // LIGHTMAP_KIND_*
uniform mat4  u_viewProj[6];    // through GLDepth::shadowProjection: depth runs 0 near, 1 far
uniform vec3  u_lightPos;
uniform vec3  u_lightDir;       // the way the light travels (sun, spot)
uniform float u_source;         // sun: tan of its angular radius; spot, point: its radius, meters
uniform float u_near;           // spot, point: the maps' near plane
uniform float u_far;            // spot, point: their far plane; sun: the depth the map spans, meters
uniform float u_texelWorld;     // sun: a map texel's meters; spot, point: per meter of distance
uniform float u_reach;          // the widest search and filter, in map texels
uniform float u_rim;            // the share of the map at each edge whose texels another tile bakes
uniform vec3  u_skyward[6];     // sky: each map's way toward the sky
uniform int   u_skyLayers;      // sky: the maps this pass takes, one to six
uniform int   u_probeCount;     // sky: the probe centers this dispatch takes, not texels; 0 for texels
uniform vec3  u_probeAt[MAX_PROBES];
uniform float u_skyShare;       // sky: what a direction hidden head-on takes off the sky seen

// Taps a blocker search and a filter take; far more than a frame's, as a bake runs once.
const int   SEARCH_TAPS        = 48;
const int   FILTER_TAPS        = 128;
// How far above the receiver a caster is looked for, as a multiple of the source: the sun's
// reach in meters, a local light's in its own radii.
const float SUN_SEARCH_RISE    = 40.0;
const float LOCAL_SEARCH_RISE  = 8.0;
// The sky's filter, each direction's: many directions overlap into the penumbra a sun's own gives.
const int   SKY_TAPS           = 8;
const float SKY_RADIUS         = 1.5;  // map texels

// Distance from the light of what a depth @p d of the map says, meters along its axis.
float distanceOf(float d) {
    return u_kind == LIGHTMAP_KIND_SUN ? d * u_far : perspectiveAxis(u_near, u_far, d);
}

// d(depth)/d(uv) of the receiver's plane in the map, so a tap off the texel compares against the
// surface where it falls: a wide search or filter under a grazing light then neither finds the
// receiver's own far side as a blocker nor shades it. From points of the plane @p eps meters off.
vec2 receiverSlope(vec3 at, vec3 N, int layer, float eps) {
    // The map's uv is half its NDC; its depth is the NDC's own.
    const vec2 slope = 2.0 * receiverSlopeNDC(u_viewProj[layer], at, N, eps);
    return clamp(slope, vec2(-64.0), vec2(64.0));
}

// The sky at @p texel, whose surface is at @p P facing @p N: each of the pass's directions above
// its horizon a caster hides takes its cosine's share off what it has left, and the page takes
// what remains.
void bakeSky(ivec2 texel, vec3 P, vec3 N) {
    const vec2 mapSize = vec2(textureSize(u_depth, 0).xy);
    float hidden = 0.0;
    for (int layer = 0; layer < u_skyLayers; ++layer) {
        const vec3  toSky  = u_skyward[layer];
        const float facing = dot(N, toSky);
        if (facing <= 0.0) continue;
        // Off the surface by a texel, and toward the sky by one: the compares follow its plane.
        const vec3 lifted = P + (N + toSky) * u_texelWorld;
        const vec3 ndc    = (u_viewProj[layer] * vec4(lifted, 1.0)).xyz;
        const vec2 uv     = ndc.xy * 0.5 + 0.5;
        const vec2 slope  = receiverSlope(lifted, N, layer, 4.0 * u_texelWorld);
        float lit = 0.0;
        for (int i = 0; i < SKY_TAPS; ++i) {
            const vec2 at    = uv + vogelTap(i, SKY_TAPS) * SKY_RADIUS / mapSize;
            const vec4 d     = textureGather(u_depth, vec3(at, float(layer)), 0);
            const vec4 there = vec4(ndc.z + dot(slope, at - uv));
            lit += dot(gatherWeights(at, mapSize), vec4(greaterThanEqual(d, there)));
        }
        hidden += (1.0 - lit / float(SKY_TAPS)) * facing;
    }
    const ivec3 at   = ivec3(texel, u_page);
    const float left = max(imageLoad(u_skyLeft, at).r - hidden * u_skyShare, 0.0);
    imageStore(u_skyLeft, at, vec4(left));
    imageStore(u_sky, at, vec4(left));
}

// The sky at probe center @p i, as a texel there facing up would see it: each of the pass's directions
// a caster hides takes its share off what the probe has left.
void bakeProbeSky(int i) {
    const vec2 mapSize = vec2(textureSize(u_depth, 0).xy);
    float hidden = 0.0;
    for (int layer = 0; layer < u_skyLayers; ++layer) {
        const vec3 ndc = (u_viewProj[layer] * vec4(u_probeAt[i], 1.0)).xyz;
        const vec2 uv  = ndc.xy * 0.5 + 0.5;
        float lit = 0.0;
        for (int t = 0; t < SKY_TAPS; ++t) {
            const vec2 at = uv + vogelTap(t, SKY_TAPS) * SKY_RADIUS / mapSize;
            const vec4 d  = textureGather(u_depth, vec3(at, float(layer)), 0);
            lit += dot(gatherWeights(at, mapSize), vec4(greaterThanEqual(d, vec4(ndc.z))));
        }
        hidden += (1.0 - lit / float(SKY_TAPS)) * u_skyward[layer].y;
    }
    b_probeSky[i] = max(b_probeSky[i] - hidden * u_skyShare, 0.0);
}

void main() {
    if (u_probeCount > 0) {
        const int i = int(gl_LocalInvocationIndex);
        if (gl_WorkGroupID.xy == uvec2(0) && i < u_probeCount) bakeProbeSky(i);
        return;
    }
    const ivec2 texel = ivec2(gl_GlobalInvocationID.xy) + u_origin;
    const ivec2 size  = textureSize(u_position, 0);
    if (texel.x >= size.x || texel.y >= size.y) return;
    const vec4 position = texelFetch(u_position, texel, 0);
    if (position.w == 0.0) return;

    const vec3 P = position.xyz;
    const vec3 N = normalize(texelFetch(u_normal, texel, 0).xyz);
    if (u_kind == LIGHTMAP_KIND_SKY) {
        bakeSky(texel, P, N);
        return;
    }
    const vec3 toLight = u_kind == LIGHTMAP_KIND_SUN ? -u_lightDir : normalize(u_lightPos - P);

    // A point light's face: the one its axis is nearest.
    int layer = 0;
    if (u_kind == LIGHTMAP_KIND_POINT) {
        const vec3 d = P - u_lightPos;
        const vec3 a = abs(d);
        layer = (a.x >= a.y && a.x >= a.z) ? (d.x > 0.0 ? 0 : 1)
            : (a.y >= a.z) ? (d.y > 0.0 ? 2 : 3)
            : (d.z > 0.0 ? 4 : 5);
    }

    // Off the surface by what the map's texels can be wrong by, toward the light. A texel's point
    // is its own surface's, so no more: a further lift slides the shadow along the ground.
    const float range    = u_kind == LIGHTMAP_KIND_SUN ? 1.0 : length(u_lightPos - P);
    const float mapTexel = u_kind == LIGHTMAP_KIND_SUN ? u_texelWorld : u_texelWorld * range;
    const vec3  lifted   = P + N * (2.0 * mapTexel) + toLight * mapTexel;

    const vec4 clip = u_viewProj[layer] * vec4(lifted, 1.0);
    const vec3 ndc  = clip.xyz / clip.w;
    const vec2 uv   = ndc.xy * 0.5 + 0.5;
    // A texel off the map, or in a rim another tile bakes, is left as it is: lit, or that tile's.
    if (clip.w <= 0.0 || any(lessThan(uv, vec2(u_rim))) || any(greaterThan(uv, vec2(1.0 - u_rim)))) {
        return;
    }
    float lit = 1.0;
    if (ndc.z <= 1.0) {
        const vec2  mapSize  = vec2(textureSize(u_depth, 0).xy);
        const float ref      = ndc.z;
        const float receiver = distanceOf(ref);
        const vec2  slope    = receiverSlope(lifted, N, layer, 4.0 * mapTexel);

        // The casters nearer the light, within reach of the source.
        const float rise   = u_kind == LIGHTMAP_KIND_SUN ? SUN_SEARCH_RISE : LOCAL_SEARCH_RISE;
        const float search = clamp(u_source * rise / mapTexel, 1.0, u_reach);
        float sum   = 0.0;
        float count = 0.0;
        for (int i = 0; i < SEARCH_TAPS; ++i) {
            const vec2 at = uv + vogelTap(i, SEARCH_TAPS) * search / mapSize;
            const vec4 d  = textureGather(u_depth, vec3(at, float(layer)), 0);
            const vec4 w  = gatherWeights(at, mapSize) * vec4(lessThan(d, vec4(ref + dot(slope, at - uv))));
            for (int k = 0; k < 4; ++k) sum += distanceOf(d[k]) * w[k];
            count += dot(w, vec4(1.0));
        }

        if (count > 0.0) {
            const float blocker  = sum / count;
            const float gap      = max(receiver - blocker, 0.0);
            const float penumbra = u_kind == LIGHTMAP_KIND_SUN ? gap * u_source
                : u_source * gap / max(blocker, u_near);
            const float radius   = clamp(penumbra / mapTexel, 1.0, u_reach);
            lit = 0.0;
            for (int i = 0; i < FILTER_TAPS; ++i) {
                const vec2 at = uv + vogelTap(i, FILTER_TAPS) * radius / mapSize;
                const vec4 d  = textureGather(u_depth, vec3(at, float(layer)), 0);
                const vec4 there = vec4(ref + dot(slope, at - uv));
                lit += dot(gatherWeights(at, mapSize), vec4(greaterThanEqual(d, there)));
            }
            lit /= float(FILTER_TAPS);
        }
    }

    const ivec3 at   = ivec3(texel, u_page);
    vec4        mask = imageLoad(u_mask, at);
    mask[u_channel]  = lit;
    imageStore(u_mask, at, mask);
}
