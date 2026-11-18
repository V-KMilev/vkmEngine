/**
 * Shadow sampling: the ShadowBlock UBO, the atlas/cube samplers and the
 * per-light-type lookups, shared so a surface and the fog see the same shadows.
 *
 * Every 2D lookup names its level: control flow varies per fragment, where implicit
 * derivatives are undefined, and the maps have level 0 alone. The cube array's compare has no
 * Lod form (sampleCube).
 * MAX_SHADOW_CASTERS_2D / _CUBE come from the shader prelude (lighting.md,
 * "Limits and the shader prelude").
 */

#include "constants.glsl"
#include "camera.glsl"
#include "basis.glsl"
#include "depth.glsl"

// Each component as Shadow2DGPU and ShadowCubeGPU say (src/backend/opengl/frame/light/gl_shadow_block.h).
struct Shadow2D {
    mat4 lightVP;
    vec4 atlas;
    vec4 params;
    vec4 shape;
};
struct ShadowCube {
    vec4 posRange;
    vec4 params;
};

layout(std140, binding = UBO_SHADOW) uniform ShadowBlock {
    vec4 cascadeSplits;  // how far from the eye each cascade reaches
    int  csmBase;        // first 2D slot of what moves under the sun, a tile a cascade (-1 = no sun)
    int  csmCount;       // active cascades
    int  heldCount;      // how many of them have a held layer of what is fixed under the sun
    int  moverCount;     // how many of them have anything in their tile
    Shadow2D   held[NUM_CASCADES];  // a layer of u_shadowHeld each, read wrapping (GLShadowPlan)
    Shadow2D   s2d[MAX_SHADOW_CASTERS_2D];
    ShadowCube scube[MAX_SHADOW_CASTERS_CUBE];
} u_shadow;

layout(binding = SHADOW_SLOT_ATLAS_2D) uniform sampler2DShadow u_shadowAtlas;
layout(binding = SHADOW_SLOT_HELD) uniform sampler2DShadow u_shadowHeld;
// Every point light's cube, a layer each.
layout(binding = SHADOW_SLOT_CUBE) uniform samplerCubeArrayShadow u_shadowCube;

// A texel over the tile edge belongs to another light.
vec2 clampToTile(Shadow2D sm, vec2 uv, vec2 texel) {
    return clamp(uv, sm.atlas.xy + texel, sm.atlas.xy + sm.atlas.zw - texel);
}

// Both biases are in texels of the map at the receiver, so one value holds for every light,
// range, cascade and distance (Castano 2013). The point moves off the surface along N by
// SHADOW_NORMAL_BIAS texels times sin(theta) - nothing head-on, all of it where the light
// grazes - and the compare slides toward the light by SHADOW_BIAS texels times
// 1 + tan(theta), tan capped at 2: a texel's own depth step on a slope.
float biasSin(float ndotl) {
    float nl = clamp(ndotl, 0.0, 1.0);
    return sqrt(1.0 - nl * nl);
}

// The slide toward the light, in meters, for @p depthTexels texels of @p texel meters.
float biasSlide(float depthTexels, float texel, float ndotl) {
    float nl = max(clamp(ndotl, 0.0, 1.0), 1e-3);
    return depthTexels * texel * (1.0 + min(biasSin(ndotl) / nl, 2.0));
}

// A tile's texel in meters at the point: a spot's scales from its size at the range
// (params.y) to the point's distance along the axis.
float tileTexel(Shadow2D sm, vec3 worldPos) {
    if (sm.shape.y <= 0.0) return sm.params.y;
    return sm.params.y * max((sm.lightVP * vec4(worldPos, 1.0)).w, sm.shape.y) / sm.params.z;
}

// Atlas UV and biased reference depth in one 2D tile, false outside. N = vec3(0) skips the normal
// offset, for a volumetric sample.
bool projectToTile(Shadow2D sm, vec3 worldPos, vec3 N, float ndotl, out vec2 atlasUV, out float ref) {
    bool perspective = sm.shape.y > 0.0;
    vec4 lc = sm.lightVP * vec4(worldPos, 1.0);
    if (lc.w <= 0.0) return false;

    // The normal offset and the slide, in this tile's own texels.
    float texel = tileTexel(sm, worldPos);
    float slide = biasSlide(sm.params.x, texel, ndotl);
    lc += sm.lightVP * vec4(N * (sm.shape.x * texel * biasSin(ndotl)), 0.0);
    if (lc.w <= 0.0) return false;
    vec3 proj = lc.xyz / lc.w * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) {
        return false;
    }
    atlasUV = sm.atlas.xy + proj.xy * sm.atlas.zw;

    if (perspective) {
        // The slide along the ray shortens the axis distance by slide times
        // the ray's cosine off axis, recovered through shape.z.
        vec2  ndc    = proj.xy * 2.0 - 1.0;
        float cosOff = inversesqrt(1.0 + sm.shape.z * sm.shape.z * dot(ndc, ndc));
        ref = perspectiveDepth(sm.shape.y, sm.params.z, max(lc.w - slide * cosOff, sm.shape.y));
        return true;
    }
    // An orthographic depth unit is the tile's whole depth span (params.z).
    ref = proj.z - slide / sm.params.z;
    return true;
}

// d(receiver depth)/d(atlas uv) in an orthographic tile, so a tap off the point compares against
// the receiver's own plane there: a grazed surface does not shade itself across the filter.
// Clamped where the light grazes the plane.
vec2 receiverSlope(Shadow2D sm, vec3 N) {
    vec3 n = transpose(inverse(mat3(sm.lightVP))) * N;
    if (abs(n.z) < 1e-4) return vec2(0.0);
    return clamp(-n.xy / n.z, vec2(-sm.shape.w), vec2(sm.shape.w)) / sm.atlas.zw;
}

// The same slope for a spot's perspective tile, from points of the plane a few texels off the
// receiver.
vec2 receiverSlopePerspective(Shadow2D sm, vec3 worldPos, vec3 N) {
    // NDC to window halves xy and z alike, so the ratio is the tile-uv slope.
    vec2 slope = receiverSlopeNDC(sm.lightVP, worldPos, N, 4.0 * tileTexel(sm, worldPos));
    return clamp(slope, vec2(-4.0), vec2(4.0)) / sm.atlas.zw;
}

// Castano's PCF along one axis of a map's texel grid: a box of compares smoothed by each one's
// bilinear weights, three texels in two taps. Places and weights follow @p f, where the point lies
// past the texel center below it, so an edge glides with the point rather than stepping. @p at is
// each tap's offset in texels from that center; @p weight sums to one.
void filterAxis(float f, out vec2 at, out vec2 weight) {
    vec2 n = vec2(3.0 - 2.0 * f, 1.0 + 2.0 * f);
    at     = vec2(2.0 - f, f) / n + vec2(-1.0, 1.0);
    weight = n / 4.0;
}

// One tap of filterTile: the compare at @p offset texels from @p center, against the receiver's
// plane there. A tile that @p wraps shows a place of the light's grid at its coordinates modulo its
// edge (GLShadowPlan), so a tap past the edge comes round; the clamp's texel off is nothing to see.
float tileTap(
    sampler2DShadow map,
    Shadow2D sm,
    vec2 center,
    vec2 offset,
    vec2 texel,
    vec2 uv,
    float ref,
    vec2 slope,
    bool wraps
) {
    vec2  at    = (center + offset) * texel;
    float plane = ref + dot(slope, at - uv);
    if (wraps) at = sm.atlas.xy + fract((at - sm.atlas.xy) / sm.atlas.zw) * sm.atlas.zw;
    return textureLod(map, vec3(clampToTile(sm, at, texel), plane), 0.0);
}

// PCF of tile @p sm of @p map about @p uv (filterAxis), 1 lit .. 0 shadowed. Each tap compares against
// the receiver's plane where it falls (@p slope), so a slope does not shade itself across the kernel.
float filterTile(sampler2DShadow map, Shadow2D sm, vec2 uv, float ref, vec2 slope, bool wraps) {
    vec2 size   = vec2(textureSize(map, 0));
    vec2 texel  = 1.0 / size;
    vec2 grid   = uv * size + 0.5;
    vec2 center = floor(grid) - 0.5;
    vec2 atX, wX, atY, wY;
    filterAxis(grid.x - floor(grid.x), atX, wX);
    filterAxis(grid.y - floor(grid.y), atY, wY);
    float lit = 0.0;
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            vec2 at = vec2(atX[i], atY[j]);
            lit += wX[i] * wY[j] * tileTap(map, sm, center, at, texel, uv, ref, slope, wraps);
        }
    }
    return lit;
}

// One tile's shadow at the point, 1 lit .. 0 shadowed; off the map reads lit.
float sample2DSlot(int slot, vec3 worldPos, vec3 N, float ndotl) {
    Shadow2D sm = u_shadow.s2d[slot];
    vec2  atlasUV;
    float ref;
    if (!projectToTile(sm, worldPos, N, ndotl, atlasUV, ref)) return 1.0;

    // A volume sample (N = 0) has no plane.
    vec2 slope = vec2(0.0);
    if (dot(N, N) > 0.0) {
        slope = sm.shape.y > 0.0 ? receiverSlopePerspective(sm, worldPos, N) : receiverSlope(sm, N);
    }
    return filterTile(u_shadowAtlas, sm, atlasUV, ref, slope, false);
}

// What is fixed under the sun at the point, in one cascade's held layer, 1 lit .. 0 shadowed. Past
// its depth's far end is behind every caster it holds, so a receiver there compares at that end.
float sampleHeld(int cascade, vec3 worldPos, vec3 N, float ndotl) {
    Shadow2D sm     = u_shadow.held[cascade];
    float    texel  = sm.params.y;
    vec3     offset = N * (sm.shape.x * texel * biasSin(ndotl));
    vec3     proj   = (sm.lightVP * vec4(worldPos + offset, 1.0)).xyz * 0.5 + 0.5;
    float    ref    = min(proj.z - biasSlide(sm.params.x, texel, ndotl) / sm.params.z, 1.0);
    // A volume sample (N = 0) has no plane.
    vec2 slope = dot(N, N) > 0.0 ? receiverSlope(sm, N) : vec2(0.0);
    vec2 uv    = sm.atlas.xy + proj.xy * sm.atlas.zw;
    return filterTile(u_shadowHeld, sm, uv, ref, slope, true);
}

// What moves under the sun at the point, in one cascade's tile, 1 lit .. 0 shadowed. Its texels are
// a held layer's twice over; it takes the filter wherever its light has a size and soft shadows are
// on (params.w), otherwise one compare.
float sampleMovers(int cascade, vec3 worldPos, vec3 N, float ndotl) {
    Shadow2D sm = u_shadow.s2d[u_shadow.csmBase + cascade];
    vec2  atlasUV;
    float ref;
    if (!projectToTile(sm, worldPos, N, ndotl, atlasUV, ref)) return 1.0;
    if (sm.params.w > 0.0) {
        vec2 slope = dot(N, N) > 0.0 ? receiverSlope(sm, N) : vec2(0.0);
        return filterTile(u_shadowAtlas, sm, atlasUV, ref, slope, false);
    }
    vec2 texel = 1.0 / vec2(textureSize(u_shadowAtlas, 0));
    return textureLod(u_shadowAtlas, vec3(clampToTile(sm, atlasUV, texel), ref), 0.0);
}

// The share of the way to a cascade's reach, from the last's, blended into the next, so the
// texel-size step draws no line across the ground.
const float CASCADE_BLEND = 0.1;

// The share of the sun's shadow distance over which its shadow fades out, so the reach ends
// in a ramp rather than a line across the ground.
const float SHADOW_FADE = 0.2;

// The tightest cascade whose reach holds the point, -1 with no sun shadow, given the point's distance
// from the eye. @p toNext runs 0..1 across the far blend band; 0 in the last cascade.
int cascadeOf(float eyeDistance, out float toNext) {
    toNext = 0.0;
    if (u_shadow.csmBase < 0 || u_shadow.csmCount <= 0) return -1;
    int ci = u_shadow.csmCount - 1;
    for (int i = 0; i < u_shadow.csmCount; ++i) {
        if (eyeDistance <= u_shadow.cascadeSplits[i]) { ci = i; break; }
    }
    if (ci < u_shadow.csmCount - 1) {
        float nearSplit = ci > 0 ? u_shadow.cascadeSplits[ci - 1] : 0.0;
        float farSplit  = u_shadow.cascadeSplits[ci];
        float band      = (farSplit - nearSplit) * CASCADE_BLEND;
        toNext = clamp((eyeDistance - (farSplit - band)) / band, 0.0, 1.0);
    }
    return ci;
}

// The sun's shadow, blended across the cascade band and faded at its reach into @p beyond, which it
// is with no sun shadow. What moves, read from its tile; what is fixed, from the held layer: the
// darker of the two, as either alone shadows the point.
float sampleCSMOver(vec3 worldPos, vec3 N, float ndotl, float beyond) {
    float eyeDistance = length(worldPos - u_camera.cameraPosition.xyz);
    float toNext;
    int   ci = cascadeOf(eyeDistance, toNext);
    if (ci < 0) return beyond;
    float far   = u_shadow.cascadeSplits[u_shadow.csmCount - 1];
    float reach = 1.0 - smoothstep(far * (1.0 - SHADOW_FADE), far, eyeDistance);
    if (reach <= 0.0) return beyond;
    // One call site for both cascades of the band: each place a filter is called from compiles a
    // copy of it.
    int   last = toNext > 0.0 ? ci + 1 : ci;
    float lit  = 1.0;
    for (int c = ci; c <= last; ++c) {
        float one = c < u_shadow.moverCount ? sampleMovers(c, worldPos, N, ndotl) : 1.0;
        if (c < u_shadow.heldCount && one > 0.0) {
            one = min(one, sampleHeld(c, worldPos, N, ndotl));
        }
        lit = c == ci ? one : mix(lit, one, toNext);
    }
    return mix(beyond, lit, reach);
}

// The distance along a cube face's axis, which is what that face's depth stores.
float majorAxis(vec3 v) {
    vec3 a = abs(v);
    return max(a.x, max(a.y, a.z));
}

// One texel of a cube face, in meters, at major-axis distance @p axis: a 90-degree face is
// 2 * axis wide.
float cubeTexel(float axis) {
    return 2.0 * axis / float(textureSize(u_shadowCube, 0).x);
}

// The depth a cube stores for the receiver's own plane along @p dir (from the light), slid
// @p slide meters toward the light. Each tap of a filter compares its own point of that
// plane, so the filter does not shade the receiver. Where the light grazes the plane the hit is
// held near the tap's own distance.
float cubeReference(vec3 dir, vec3 onPlane, vec3 N, float slide, float nearPlane, float farPlane) {
    float dn = dot(dir, N);
    float t  = abs(dn) > 1e-4 ? clamp(dot(onPlane, N) / dn, 0.5, 2.0) : 1.0;
    vec3  q  = dir * t;
    float r  = length(q);
    float axis = majorAxis(q) * max(r - slide, nearPlane) / max(r, 1e-6);
    return perspectiveDepth(nearPlane, farPlane, axis);
}

// The cube's compare along @p dir from the light, against the receiver's plane there
// (cubeReference).
float cubeTap(vec3 dir, vec3 rel, vec3 N, float layer, float slide, float nearPlane, float farPlane) {
    return texture(u_shadowCube, vec4(dir, layer), cubeReference(dir, rel, N, slide, nearPlane, farPlane));
}

// PCF of a point light's cube about @p rel, with filterAxis's taps on the grid of the face the
// point falls on, each at the point's own distance along the face's axis. A tap past the face's
// edge reads across it, as the cube's compares do.
float filterCube(vec3 rel, vec3 N, float layer, float slide, float nearPlane, float farPlane) {
    // The face: the two axes its texels run along, whose signs do not move the grid, and its own.
    vec3 a = abs(rel);
    mat3 face;
    if (a.x >= a.y && a.x >= a.z) {
        face = mat3(vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(sign(rel.x), 0.0, 0.0));
    } else if (a.y >= a.z) {
        face = mat3(vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, sign(rel.y), 0.0));
    } else {
        face = mat3(vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, sign(rel.z)));
    }
    vec3  onFace = rel * face;
    float axis   = max(onFace.z, 1e-6);
    float size   = float(textureSize(u_shadowCube, 0).x);
    vec2  grid   = (onFace.xy / axis * 0.5 + 0.5) * size + 0.5;
    vec2  center = floor(grid) - 0.5;
    vec2  atX, wX, atY, wY;
    filterAxis(grid.x - floor(grid.x), atX, wX);
    filterAxis(grid.y - floor(grid.y), atY, wY);
    float lit = 0.0;
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            vec3 dir = face * (vec3((center + vec2(atX[i], atY[j])) / size * 2.0 - 1.0, 1.0) * axis);
            lit += wX[i] * wY[j] * cubeTap(dir, rel, N, layer, slide, nearPlane, farPlane);
        }
    }
    return lit;
}

// A point light's shadow in one compare, slid by its depth bias but with no normal offset and no
// filter, for a point with no surface to offset from.
float sampleCubeHard(int slot, vec3 worldPos) {
    ShadowCube sc = u_shadow.scube[slot];
    vec3 rel = worldPos - sc.posRange.xyz;
    if (length(rel) > sc.posRange.w) return 1.0;
    float slide = biasSlide(sc.params.x, cubeTexel(majorAxis(rel)), 1.0);
    float ref   = cubeReference(rel, rel, vec3(0.0), slide, sc.params.y, sc.posRange.w);
    return texture(u_shadowCube, vec4(rel, float(slot)), ref);
}

// Point light, biased as a tile is (biasSlide) in the cube's texels at the receiver.
//
// The cube array takes the slot as a layer, so the index need not be uniform. It has one
// level, filtered linearly both ways, so the derivative a compare implies chooses nothing.
float sampleCube(int slot, vec3 worldPos, vec3 N, float ndotl) {
    ShadowCube sc = u_shadow.scube[slot];   // a UBO array takes any index
    float farPlane  = sc.posRange.w;
    vec3  rel       = worldPos - sc.posRange.xyz;
    if (length(rel) > farPlane) return 1.0;

    float texel = cubeTexel(majorAxis(rel));
    rel += N * (sc.params.z * texel * biasSin(ndotl));
    float slide = biasSlide(sc.params.x, texel, ndotl);
    return filterCube(rel, N, float(slot), slide, sc.params.y, farPlane);
}

// A light's shadow on a surface: the sun's cascades, a spot's tile, a point light's cube; lit for a
// light with no shadow slot. @p Ng is the geometric normal, which the biases follow. The sun's fades
// at its reach into @p sunBeyond (sampleCSMOver).
float surfaceShadow(int type, int slot, vec3 worldPos, vec3 Ng, float ndotl, float sunBeyond) {
    if (slot < 0) return 1.0;
    if (type == LIGHT_DIRECTIONAL) return sampleCSMOver(worldPos, Ng, ndotl, sunBeyond);
    if (type == LIGHT_SPOT)        return sample2DSlot(slot, worldPos, Ng, ndotl);
    if (type == LIGHT_POINT)       return sampleCube(slot, worldPos, Ng, ndotl);
    return 1.0;
}

// A light's shadow at a point with no surface, with no normal to offset along: one compare for a point
// light, filtered as at a surface for a spot and the sun; lit for a light with no shadow slot.
float pointShadow(int type, int slot, vec3 worldPos) {
    if (slot < 0) return 1.0;
    if (type == LIGHT_DIRECTIONAL) return sampleCSMOver(worldPos, vec3(0.0), 1.0, 1.0);
    if (type == LIGHT_SPOT)        return sample2DSlot(slot, worldPos, vec3(0.0), 1.0);
    if (type == LIGHT_POINT)       return sampleCubeHard(slot, worldPos);
    return 1.0;
}
