/**
 * Shadow sampling: the ShadowBlock UBO, the atlas/cube samplers and the
 * per-light-type lookups, shared so a surface and the fog see the same shadows.
 *
 * Every lookup names its level: control flow varies per fragment, where implicit
 * derivatives are undefined, and the maps have level 0 alone.
 * MAX_SHADOW_CASTERS_2D / _CUBE come from the shader prelude (lighting.md,
 * "Limits and the shader prelude").
 */

#include "constants.glsl"
#include "camera.glsl"

struct Shadow2D {
    mat4 lightVP;   // world -> light clip space
    vec4 atlas;     // xy = tile UV offset, zw = tile UV scale
    vec4 params;    // x = depth bias in texels (biasSlide),
                    // y = world texel size (a spot's at its range),
                    // z = far plane: an ortho tile's depth span, a spot's range,
                    // w = source size, 0 = hard: tan(sun angular radius) for a cascade,
                    //     source radius over the map's width one metre out for a spot
    vec4 shape;     // x = normal-offset bias in texels,
                    // y = a spot's near plane, 0 = orthographic,
                    // z = tan of a spot's half field of view
};
struct ShadowCube {
    vec4 posRange;  // xyz = light world pos, w = range (the faces' far plane)
    vec4 params;    // x = depth bias in texels, y = the faces' near plane,
                    // z = normal-offset bias in texels, w = source radius in metres, 0 = hard
};

layout(std140, binding = UBO_SHADOW) uniform ShadowBlock {
    vec4 cascadeSplits;  // view-space far depth per cascade
    int  csmBase;        // first 2D slot of the sun's cascade run (-1 = no sun)
    int  csmCount;       // active cascades
    int  pad0;
    int  pad1;
    Shadow2D   s2d[MAX_SHADOW_CASTERS_2D];
    ShadowCube scube[MAX_SHADOW_CASTERS_CUBE];
} u_shadow;

layout(binding = SHADOW_SLOT_ATLAS_2D) uniform sampler2DShadow u_shadowAtlas;
// Every point light's cube, a layer each, compared and as stored depth.
layout(binding = SHADOW_SLOT_CUBE) uniform samplerCubeArrayShadow u_shadowCube;
layout(binding = SHADOW_SLOT_CUBE_RAW) uniform samplerCubeArray u_shadowCubeRaw;
// The same atlas, read as depth.
layout(binding = SHADOW_SLOT_ATLAS_2D_RAW) uniform sampler2D u_shadowAtlasRaw;

// A texel over the tile edge belongs to another light.
vec2 clampToTile(Shadow2D sm, vec2 uv, vec2 texel) {
    return clamp(uv, sm.atlas.xy + texel, sm.atlas.xy + sm.atlas.zw - texel);
}

// Both biases are in texels of the map at the receiver, so one value holds for every light,
// range, cascade and distance (Castano 2013). The point moves off the surface along N by
// shadowNormalBias texels times sin(theta) - nothing head-on, all of it where the light
// grazes - and the compare slides toward the light by shadowBias texels times
// 1 + tan(theta), tan capped at 2: a texel's own depth step on a slope.
float biasSin(float ndotl) {
    float nl = clamp(ndotl, 0.0, 1.0);
    return sqrt(1.0 - nl * nl);
}

// The slide toward the light, in metres, for @p depthTexels texels of @p texel metres.
float biasSlide(float depthTexels, float texel, float ndotl) {
    float nl = max(clamp(ndotl, 0.0, 1.0), 1e-3);
    return depthTexels * texel * (1.0 + min(biasSin(ndotl) / nl, 2.0));
}

// The window depth a perspective map stores at a distance along its axis.
float perspectiveDepth(float nearPlane, float farPlane, float axis) {
    return farPlane / (farPlane - nearPlane) * (1.0 - nearPlane / axis);
}

// A tile's texel in metres at the point: a spot's scales from its size at the range
// (params.y) to the point's distance along the axis.
float tileTexel(Shadow2D sm, vec3 worldPos) {
    if (sm.shape.y <= 0.0) return sm.params.y;
    return sm.params.y * max((sm.lightVP * vec4(worldPos, 1.0)).w, sm.shape.y) / sm.params.z;
}

// Atlas UV and biased reference depth in one 2D tile, false outside; shared by
// the hard and soft paths so they agree on bias. N = vec3(0) skips the normal
// offset, for a volumetric sample. @p slide is the depth bias in metres (biasSlide),
// so the soft path can read a coarser cascade with the point's own.
bool projectToTileBiased(
    Shadow2D sm,
    vec3 worldPos,
    vec3 N,
    float ndotl,
    float slide,
    out vec2 atlasUV,
    out float ref
) {
    bool perspective = sm.shape.y > 0.0;
    vec4 lc = sm.lightVP * vec4(worldPos, 1.0);
    if (lc.w <= 0.0) return false;

    // The normal offset, in this tile's own texels.
    lc += sm.lightVP * vec4(N * (sm.shape.x * tileTexel(sm, worldPos) * biasSin(ndotl)), 0.0);
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

bool projectToTile(Shadow2D sm, vec3 worldPos, vec3 N, float ndotl, out vec2 atlasUV, out float ref) {
    float slide = biasSlide(sm.params.x, tileTexel(sm, worldPos), ndotl);
    return projectToTileBiased(sm, worldPos, N, ndotl, slide, atlasUV, ref);
}

// d(receiver depth)/d(atlas uv) in an orthographic tile, so a distant tap
// compares against the receiver's own plane there: a grazed surface neither
// finds itself in the blocker search (collapsing penumbrae) nor self-shadows
// under a wide filter. Clamped where the light grazes the plane.
vec2 receiverSlope(Shadow2D sm, vec3 N) {
    vec3 n = transpose(inverse(mat3(sm.lightVP))) * N;
    if (abs(n.z) < 1e-4) return vec2(0.0);
    return clamp(-n.xy / n.z, vec2(-4.0), vec2(4.0)) / sm.atlas.zw;
}

// The same slope for a spot's perspective tile, whose window depth is affine in its own uv
// over a plane too: two points of the plane a few texels off the receiver give it exactly.
vec2 receiverSlopePerspective(Shadow2D sm, vec3 worldPos, vec3 N) {
    vec3  T = normalize(cross(abs(N.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), N));
    vec3  B = cross(N, T);
    float e = 4.0 * tileTexel(sm, worldPos);
    vec4  a = sm.lightVP * vec4(worldPos, 1.0);
    vec4  b = sm.lightVP * vec4(worldPos + T * e, 1.0);
    vec4  c = sm.lightVP * vec4(worldPos + B * e, 1.0);
    vec3  pa = a.xyz / a.w;
    vec3  pb = b.xyz / b.w;
    vec3  pc = c.xyz / c.w;
    mat2  J  = mat2(pb.xy - pa.xy, pc.xy - pa.xy);
    if (abs(determinant(J)) < 1e-10) return vec2(0.0);
    // NDC to window halves xy and z alike, so the ratio is the tile-uv slope.
    vec2 slope = inverse(transpose(J)) * vec2(pb.z - pa.z, pc.z - pa.z);
    return clamp(slope, vec2(-4.0), vec2(4.0)) / sm.atlas.zw;
}

// 3x3 PCF of one tile, 1 lit .. 0 shadowed; off-map reads lit. Hardware
// compares give a bilinear fraction each, so the ratio is continuous where nine
// nearest fetches would staircase once a texel outgrows a pixel.
float sample2DSlot(int slot, vec3 worldPos, vec3 N, float ndotl) {
    Shadow2D sm = u_shadow.s2d[slot];
    vec2  atlasUV;
    float ref;
    if (!projectToTile(sm, worldPos, N, ndotl, atlasUV, ref)) return 1.0;
    vec2 texel = 1.0 / vec2(textureSize(u_shadowAtlas, 0));

    // Each tap compares against the receiver's plane where it falls, so a slope does not shade
    // itself across the kernel; a volume sample (N = 0) has no plane.
    vec2 slope = vec2(0.0);
    if (dot(N, N) > 0.0) {
        slope = sm.shape.y > 0.0 ? receiverSlopePerspective(sm, worldPos, N) : receiverSlope(sm, N);
    }

    float lit = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 at = clampToTile(sm, atlasUV + vec2(x, y) * texel, texel);
            lit += textureLod(u_shadowAtlas, vec3(at, ref + dot(slope, at - atlasUV)), 0.0);
        }
    }
    return lit / 9.0;
}

// One tap of a Vogel disk. Never turned by per-pixel noise: that grain needs a
// temporal filter, which this engine lacks. A disk dense enough that its 2x2
// compares overlap gives a smooth edge unturned.
vec2 vogelTap(int i, int n) {
    float r     = sqrt((float(i) + 0.5) / float(n));
    float theta = float(i) * GOLDEN_ANGLE;
    return r * vec2(cos(theta), sin(theta));
}

const int   SOFT_SEARCH_TAPS = 16;    // each a gather of four texels
const int   SOFT_MIN_TAPS    = 12;
const int   SOFT_MAX_TAPS    = 64;
const float SOFT_MIN_TEXELS  = 1.0;   // never narrower than the hardware compare's own 2x2
// Widest tap spacing in texels at which the 2x2 compares still overlap; further
// apart the edge breaks into dots.
const float SOFT_TAP_SPACING = 1.25;
// Widest disk in a cascade, in tile texels: what SOFT_MAX_TAPS fill at
// SOFT_TAP_SPACING. A wider penumbra reads a coarser cascade.
const float SOFT_REACH       = 5.5;
// A spot's map has no coarser copy, so its disk spreads a little further.
const float SOFT_SPOT_REACH  = 7.0;

// The tile's size in its own texels.
float tileTexels(Shadow2D sm) {
    return sm.atlas.z * float(textureSize(u_shadowAtlas, 0).x);
}

// Blocker search (Fernando 2005): average depth of what lies nearer the light
// within radius texels of uv, or -1 for none. A gather reads four depths, not
// their blend, for one read's price; weighted bilinearly, so the average, and
// the penumbra drawn from it, does not step from texel to texel.
float searchBlockers(Shadow2D sm, vec2 uv, float ref, vec2 slope, float radius) {
    vec2  size  = vec2(textureSize(u_shadowAtlas, 0));
    vec2  texel = 1.0 / size;
    float sum   = 0.0;
    float count = 0.0;
    // The centre first: the disk's own taps start off it and can step past a thin caster.
    for (int i = -1; i < SOFT_SEARCH_TAPS; ++i) {
        vec2 tap    = i < 0 ? vec2(0.0) : vogelTap(i, SOFT_SEARCH_TAPS);
        vec2 at     = clampToTile(sm, uv + tap * radius * texel, texel);
        vec4 d      = textureGather(u_shadowAtlasRaw, at, 0);
        // Gather order: (0,1) (1,1) (1,0) (0,0) of the footprint.
        vec2 f      = fract(at * size - 0.5);
        vec4 w      = vec4((1.0 - f.x) * f.y, f.x * f.y, f.x * (1.0 - f.y), (1.0 - f.x) * (1.0 - f.y));
        vec4 nearer = w * vec4(lessThan(d, vec4(ref + dot(slope, at - uv))));
        sum   += dot(d, nearer);
        count += dot(nearer, vec4(1.0));
    }
    return count > 0.0 ? sum / count : -1.0;
}

// PCF over a disk of radius texels, with taps SOFT_TAP_SPACING apart.
float filterDisk(Shadow2D sm, vec2 uv, float ref, vec2 slope, float radius) {
    vec2  texel      = 1.0 / vec2(textureSize(u_shadowAtlas, 0));
    float spacedTaps = PI * radius * radius / (SOFT_TAP_SPACING * SOFT_TAP_SPACING);
    int   taps       = clamp(int(ceil(spacedTaps)), SOFT_MIN_TAPS, SOFT_MAX_TAPS);
    float lit        = 0.0;
    for (int i = 0; i < taps; ++i) {
        vec2 at = clampToTile(sm, uv + vogelTap(i, taps) * radius * texel, texel);
        lit += textureLod(u_shadowAtlas, vec3(at, ref + dot(slope, at - uv)), 0.0);
    }
    return lit / float(taps);
}

// A window depth of a perspective tile, as distance along the light's axis.
float spotDistance(Shadow2D sm, float depth) {
    float n = sm.shape.y;
    float f = sm.params.z;
    return n * f / (f - depth * (f - n));
}

// PCSS of a spot's tile. A blocker at distance b spreads the shadow at the
// receiver's distance r over source * (r - b) / b; params.w puts it in map
// units. With no coarser copy, search and filter stop at SOFT_SPOT_REACH. A hard
// tile (params.w = 0) takes the 3x3 path.
float sample2DSlotSoft(int slot, vec3 worldPos, vec3 N, float ndotl) {
    Shadow2D sm = u_shadow.s2d[slot];
    if (sm.params.w <= 0.0) return sample2DSlot(slot, worldPos, N, ndotl);
    vec2  uv;
    float ref;
    if (!projectToTile(sm, worldPos, N, ndotl, uv, ref)) return 1.0;

    // The receiver's own distance, not the biased reference's.
    float tile = tileTexels(sm);
    float r    = (sm.lightVP * vec4(worldPos, 1.0)).w;
    float n    = sm.shape.y;

    // A blocker nearer the light casts over a wider region. Each tap compares against the
    // receiver's own plane there, as a cascade's does.
    vec2  slope   = receiverSlopePerspective(sm, worldPos, N);
    float search  = min(sm.params.w * (r - n) / (r * n) * tile, SOFT_SPOT_REACH);
    float blocker = searchBlockers(sm, uv, ref, slope, max(search, SOFT_MIN_TEXELS));
    if (blocker < 0.0) return 1.0;

    float b        = spotDistance(sm, blocker);
    float penumbra = sm.params.w * (r - b) / (b * r) * tile;
    return filterDisk(sm, uv, ref, slope, clamp(penumbra, SOFT_MIN_TEXELS, SOFT_SPOT_REACH));
}

// The share of a cascade's depth, at its far end, blended into the next, so the
// texel-size step draws no line across the ground.
const float CASCADE_BLEND = 0.1;

// The share of the sun's shadow distance over which its shadow fades out, so the reach ends
// in a ramp rather than a line across the ground (Godot fades its last fifth).
const float SHADOW_FADE = 0.2;

// 1 where the sun's shadow is whole, falling to 0 at its last split.
float shadowReach(vec3 worldPos) {
    float far = u_shadow.cascadeSplits[u_shadow.csmCount - 1];
    float vd  = -(u_camera.view * vec4(worldPos, 1.0)).z;
    return 1.0 - smoothstep(far * (1.0 - SHADOW_FADE), far, vd);
}

// The tightest cascade containing the point by view depth, -1 with no sun
// shadow. @p toNext runs 0..1 across the far blend band; 0 in the last cascade.
int cascadeOf(vec3 worldPos, out float toNext) {
    toNext = 0.0;
    if (u_shadow.csmBase < 0 || u_shadow.csmCount <= 0) return -1;
    float vd = -(u_camera.view * vec4(worldPos, 1.0)).z;
    int   ci = u_shadow.csmCount - 1;
    for (int i = 0; i < u_shadow.csmCount; ++i) {
        if (vd <= u_shadow.cascadeSplits[i]) { ci = i; break; }
    }
    if (ci < u_shadow.csmCount - 1) {
        float nearSplit = ci > 0 ? u_shadow.cascadeSplits[ci - 1] : 0.0;
        float farSplit  = u_shadow.cascadeSplits[ci];
        float band      = (farSplit - nearSplit) * CASCADE_BLEND;
        toNext = clamp((vd - (farSplit - band)) / band, 0.0, 1.0);
    }
    return ci;
}

// Hard sun shadow, blended across the cascade band and faded at its reach; lit with no sun
// shadow.
float sampleCSM(vec3 worldPos, vec3 N, float ndotl) {
    float toNext;
    int   ci = cascadeOf(worldPos, toNext);
    if (ci < 0) return 1.0;
    float reach = shadowReach(worldPos);
    if (reach <= 0.0) return 1.0;
    float lit = sample2DSlot(u_shadow.csmBase + ci, worldPos, N, ndotl);
    if (toNext > 0.0) lit = mix(lit, sample2DSlot(u_shadow.csmBase + ci + 1, worldPos, N, ndotl), toNext);
    return mix(1.0, lit, reach);
}

// Soft sun shadow (PCSS) for a point in cascade @p ci: search and filter each
// run in the finest cascade from @p ci that fits their disk in SOFT_REACH
// texels, so a wide penumbra reads a coarser tile and a contact the sharpest.
// The slide is @p ci's, in metres: a coarse cascade's would lift the point clear of nearby
// casters. The normal offset is the tile's own, which its texels need against acne. A search
// that finds nothing in a coarse tile is retried in the point's own, for casters too thin for it.
float sampleCSMSoftFrom(int ci, vec3 worldPos, vec3 N, float ndotl) {
    int      base      = u_shadow.csmBase;
    Shadow2D own       = u_shadow.s2d[base + ci];
    float    tanSource = own.params.w;
    if (tanSource <= 0.0) return sample2DSlot(base + ci, worldPos, N, ndotl);

    float tile     = tileTexels(own);
    int   last     = u_shadow.csmCount - 1;
    float biasDist = biasSlide(own.params.x, own.params.y, ndotl);

    // The search radius in metres, then the finest cascade it fits.
    float reach = 0.5 * own.params.y * tile * tanSource;
    int   sc    = ci;
    while (sc < last && reach / u_shadow.s2d[base + sc].params.y > SOFT_REACH) ++sc;
    Shadow2D searched = u_shadow.s2d[base + sc];
    vec2  uv;
    float ref;
    while (!projectToTileBiased(searched, worldPos, N, ndotl, biasDist, uv, ref)) {
        if (--sc < ci) return 1.0;
        searched = u_shadow.s2d[base + sc];
    }
    // widest is the radius searched, in metres: past its rim the point reads
    // lit, so a wider filter would tear an edge into the penumbra.
    float searchTexels = clamp(reach / searched.params.y, SOFT_MIN_TEXELS, SOFT_REACH);
    float blocker      = searchBlockers(searched, uv, ref, receiverSlope(searched, N), searchTexels);
    float widest       = searchTexels * searched.params.y;
    if (blocker < 0.0 && sc > ci) {
        // A caster too thin for the coarse tile: if one compare in the own tile
        // shows it, search there. That compare's edge is hard, so the filter
        // keeps its full width, faint enough there to hide it.
        sc       = ci;
        searched = own;
        if (!projectToTile(own, worldPos, N, ndotl, uv, ref)) return 1.0;
        if (textureLod(u_shadowAtlas, vec3(uv, ref), 0.0) >= 1.0) return 1.0;
        float ownTexels = clamp(reach / own.params.y, SOFT_MIN_TEXELS, SOFT_REACH);
        blocker = searchBlockers(own, uv, ref, receiverSlope(own, N), ownTexels);
        widest  = own.params.z * tanSource;
    }
    if (blocker < 0.0) return 1.0;

    // Receiver-to-blocker metres (params.z) times the sun's tangent.
    float penumbra = min((ref - blocker) * searched.params.z * tanSource, widest);

    int fc = ci;
    while (fc < last && penumbra / u_shadow.s2d[base + fc].params.y > SOFT_REACH) ++fc;
    Shadow2D filtered = u_shadow.s2d[base + fc];
    while (!projectToTileBiased(filtered, worldPos, N, ndotl, biasDist, uv, ref)) {
        if (--fc < ci) return 1.0;
        filtered = u_shadow.s2d[base + fc];
    }
    float filterTexels = clamp(penumbra / filtered.params.y, SOFT_MIN_TEXELS, SOFT_REACH);
    return filterDisk(filtered, uv, ref, receiverSlope(filtered, N), filterTexels);
}

// sampleCSMSoftFrom, blended and faded like the hard path.
float sampleCSMSoft(vec3 worldPos, vec3 N, float ndotl) {
    float toNext;
    int   ci = cascadeOf(worldPos, toNext);
    if (ci < 0) return 1.0;
    float reach = shadowReach(worldPos);
    if (reach <= 0.0) return 1.0;
    float lit = sampleCSMSoftFrom(ci, worldPos, N, ndotl);
    if (toNext > 0.0) lit = mix(lit, sampleCSMSoftFrom(ci + 1, worldPos, N, ndotl), toNext);
    return mix(1.0, lit, reach);
}

// The distance along a cube face's axis, which is what that face's depth stores.
float majorAxis(vec3 v) {
    vec3 a = abs(v);
    return max(a.x, max(a.y, a.z));
}

// One texel of a cube face, in metres, at major-axis distance @p axis: a 90-degree face is
// 2 * axis wide.
float cubeTexel(float axis) {
    return 2.0 * axis / float(textureSize(u_shadowCube, 0).x);
}

// The depth a cube stores for the receiver's own plane along @p dir (from the light), slid
// @p slide metres toward the light. Each tap of a filter compares its own point of that
// plane, so a wide filter neither finds the receiver among its blockers nor shades it.
// Where the light grazes the plane the hit is held near the tap's own distance.
float cubeReference(vec3 dir, vec3 onPlane, vec3 N, float slide, float nearPlane, float farPlane) {
    float dn = dot(dir, N);
    float t  = abs(dn) > 1e-4 ? clamp(dot(onPlane, N) / dn, 0.5, 2.0) : 1.0;
    vec3  q  = dir * t;
    float r  = length(q);
    float axis = majorAxis(q) * max(r - slide, nearPlane) / max(r, 1e-6);
    return perspectiveDepth(nearPlane, farPlane, axis);
}

// The radial distance of what a cube stores along @p dir: the face's depth back to its axis
// distance, then along the ray.
float cubeBlockerDistance(float depth, vec3 dir, float nearPlane, float farPlane) {
    float axis = nearPlane * farPlane / (farPlane - depth * (farPlane - nearPlane));
    return axis * length(dir) / majorAxis(dir);
}

// PCF over a disk of @p radius metres across the light's ray, each tap compared against the
// receiver's plane where it falls.
float filterCube(
    vec3 rel,
    vec3 N,
    vec3 T,
    vec3 B,
    float layer,
    float radius,
    float texel,
    float slide,
    float nearPlane,
    float farPlane
) {
    float spacedTaps = PI * (radius / texel) * (radius / texel) / (SOFT_TAP_SPACING * SOFT_TAP_SPACING);
    int   taps       = clamp(int(ceil(spacedTaps)), SOFT_MIN_TAPS, SOFT_MAX_TAPS);
    float lit        = 0.0;
    for (int i = 0; i < taps; ++i) {
        vec2  o   = vogelTap(i, taps) * radius;
        vec3  dir = rel + T * o.x + B * o.y;
        float ref = cubeReference(dir, rel, N, slide, nearPlane, farPlane);
        lit += texture(u_shadowCube, vec4(dir, layer), ref);
    }
    return lit / float(taps);
}

// Blocker search across the ray (Fernando 2005): the mean distance from the light of what
// lies nearer it than the receiver's plane, within @p radius metres, or -1 for none.
float searchCubeBlockers(
    vec3 rel,
    vec3 N,
    vec3 T,
    vec3 B,
    float layer,
    float radius,
    float slide,
    float nearPlane,
    float farPlane
) {
    float sum   = 0.0;
    float count = 0.0;
    // The centre first, as searchBlockers does.
    for (int i = -1; i < SOFT_SEARCH_TAPS; ++i) {
        vec2  o     = (i < 0 ? vec2(0.0) : vogelTap(i, SOFT_SEARCH_TAPS)) * radius;
        vec3  dir   = rel + T * o.x + B * o.y;
        float depth = textureLod(u_shadowCubeRaw, vec4(dir, layer), 0.0).r;
        if (depth < cubeReference(dir, rel, N, slide, nearPlane, farPlane)) {
            sum   += cubeBlockerDistance(depth, dir, nearPlane, farPlane);
            count += 1.0;
        }
    }
    return count > 0.0 ? sum / count : -1.0;
}

// A point light's shadow at a volume sample: one compare, no offset and no filter, since a
// sample has no surface and its froxel is the filter.
float sampleCubeHard(int slot, vec3 worldPos) {
    ShadowCube sc = u_shadow.scube[slot];
    vec3 rel = worldPos - sc.posRange.xyz;
    if (length(rel) > sc.posRange.w) return 1.0;
    float slide = biasSlide(sc.params.x, cubeTexel(majorAxis(rel)), 1.0);
    float ref   = cubeReference(rel, rel, vec3(0.0), slide, sc.params.y, sc.posRange.w);
    return texture(u_shadowCube, vec4(rel, float(slot)), ref);
}

// Point light, biased as a tile is (biasSlide) in the cube's texels at the receiver. A source
// radius above zero makes it percentage-closer soft, as a spot is; zero is a hard edge,
// filtered a texel wide.
//
// The cube array takes the slot as a layer, so the index need not be uniform. It has one
// level, filtered linearly both ways, so the derivative a compare implies chooses nothing.
float sampleCube(int slot, vec3 worldPos, vec3 N, float ndotl) {
    ShadowCube sc = u_shadow.scube[slot];   // a UBO array takes any index
    float farPlane  = sc.posRange.w;
    float nearPlane = sc.params.y;
    float layer     = float(slot);
    vec3  rel       = worldPos - sc.posRange.xyz;
    if (length(rel) > farPlane) return 1.0;

    float texel = cubeTexel(majorAxis(rel));
    rel += N * (sc.params.z * texel * biasSin(ndotl));
    float slide = biasSlide(sc.params.x, texel, ndotl);
    float d     = max(length(rel), 1e-4);

    // Across the ray: the disk the taps are laid on.
    vec3 l  = rel / d;
    vec3 up = abs(l.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 T  = normalize(cross(up, l));
    vec3 B  = cross(l, T);

    float source = sc.params.w;
    if (source <= 0.0) {
        return filterCube(rel, N, T, B, layer, SOFT_MIN_TEXELS * texel, texel, slide, nearPlane, farPlane);
    }

    // A blocker nearer the light casts over a wider region; capped as a spot's is.
    float search  = min(source * (d - nearPlane) / nearPlane, SOFT_SPOT_REACH * texel);
    float blocker = searchCubeBlockers(
        rel,
        N,
        T,
        B,
        layer,
        max(search, SOFT_MIN_TEXELS * texel),
        slide,
        nearPlane,
        farPlane
    );
    if (blocker < 0.0) return 1.0;

    float penumbra = source * (d - blocker) / max(blocker, nearPlane);
    float radius   = clamp(penumbra, SOFT_MIN_TEXELS * texel, SOFT_SPOT_REACH * texel);
    return filterCube(rel, N, T, B, layer, radius, texel, slide, nearPlane, farPlane);
}
