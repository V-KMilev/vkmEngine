/**
 * Shared shadow sampling: the ShadowBlock UBO, the atlas/cube samplers, and the
 * per-light-type sampling functions.
 *
 * Included by the forward pass and by the volumetric-fog injection compute, so
 * fog scatters the sun through the *same* cascades that shadow the geometry -
 * one implementation, no chance of the two drifting apart.
 *
 * Requires MAX_SHADOW_CASTERS_2D / _CUBE, so include the generated engine config
 * before this file. Deliberately takes the camera position as a parameter rather
 * than reading a CameraBlock, so a compute shader can use it without declaring
 * the graphics camera UBO.
 */

#define SHADOW_MAX_2D   MAX_SHADOW_CASTERS_2D
#define SHADOW_MAX_CUBE MAX_SHADOW_CASTERS_CUBE

struct Shadow2D {
    mat4 lightVP;   // world -> light clip space
    vec4 atlas;     // xy = tile UV offset, zw = tile UV scale
    vec4 params;    // x = depth-compare bias, y = cascade world texel size
};
struct ShadowCube {
    vec4 posRange;  // xyz = light world pos, w = range
    vec4 params;    // x = bias
};

layout(std140, binding = 3) uniform ShadowBlock {
    vec4 camForward;     // xyz = camera forward (cascade selection)
    vec4 cascadeSplits;  // view-space far depth per cascade
    int  csmBase;        // first 2D slot of the sun's cascade run (-1 = no sun)
    int  csmCount;       // active cascades
    int  _sp0;
    int  _sp1;
    Shadow2D   s2d[SHADOW_MAX_2D];
    ShadowCube scube[SHADOW_MAX_CUBE];
} u_shadow;

layout(binding = 11) uniform sampler2DShadow u_shadowAtlas;
layout(binding = 12) uniform samplerCube u_shadowCube[SHADOW_MAX_CUBE];

// 3x3 PCF sample of one 2D atlas tile. Returns 1 (lit) .. 0 (shadowed); off-map
// or beyond-far reads as lit so geometry outside the map is never darkened.
//
// The taps go through a sampler2DShadow, so the texture unit does the depth
// compare against a 2x2 neighbourhood and returns the bilinear-weighted
// fraction that passed. That matters wherever a shadow texel is wider than a
// screen pixel - which is most of the near field at a large shadowDistance -
// because a nearest fetch compared in the shader can only return 0 or 1, and
// nine of them can only return ten distinct values. The edge then lands on
// texel boundaries and reads as a staircase of blocks however dense the map
// is. The same nine taps interpolated give a continuous ratio instead.
//
// Pass N = vec3(0) to skip the normal-offset bias - correct for a volumetric
// sample, which has no surface to self-shadow.
float sample2DSlot(int slot, vec3 worldPos, vec3 N, float ndotl) {
    Shadow2D sm = u_shadow.s2d[slot];

    // Normal-offset bias: shift the sample point along the surface normal by a
    // few shadow texels (more at grazing angles). params.y is the cascade's
    // world texel size, so this scales per cascade and kills self-shadow acne
    // without the peter-panning a large depth bias would cause.
    float offsetTexels = 1.5 + 3.0 * (1.0 - clamp(ndotl, 0.0, 1.0));
    vec3  samplePos    = worldPos + N * (sm.params.y * offsetTexels);

    vec4 lc = sm.lightVP * vec4(samplePos, 1.0);
    if (lc.w <= 0.0) return 1.0;
    vec3 proj = lc.xyz / lc.w * 0.5 + 0.5;
    if (proj.z > 1.0 ||
        proj.x < 0.0 || proj.x > 1.0 ||
        proj.y < 0.0 || proj.y > 1.0) {
        return 1.0;
    }

    // Slope-scaled depth bias on top (params.x = the light's shadowBias knob).
    // One shadow texel spans a depth range proportional to tan(theta) between
    // the surface and the light, so a constant bias only covers surfaces facing
    // the light. It is the ground under a low sun that breaks: the normal-offset
    // above shifts along the surface normal, which at grazing incidence is
    // perpendicular to the light's view direction and so barely moves the sample
    // in depth - the acne it is meant to kill reappears as stripes. Clamped, or
    // a light parallel to the surface would drive the bias to infinity and
    // detach the shadow from whatever casts it.
    float nl      = clamp(ndotl, 0.0, 1.0);
    float slope   = min(sqrt(1.0 - nl * nl) / max(nl, 0.1), 4.0);
    float bias    = sm.params.x * (1.0 + slope);
    vec2  atlasUV = sm.atlas.xy + proj.xy * sm.atlas.zw;
    vec2  texel   = 1.0 / vec2(textureSize(u_shadowAtlas, 0));
    float ref     = proj.z - bias;
    float lit     = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            lit += texture(u_shadowAtlas, vec3(atlasUV + vec2(x, y) * texel, ref));
        }
    }
    return lit / 9.0;
}

// Directional sun: pick the tightest cascade containing the point by view depth,
// then PCF-sample that cascade's tile. Lit (1.0) when the sun casts no shadow.
float sampleCSM(vec3 worldPos, vec3 N, float ndotl, vec3 cameraPos) {
    if (u_shadow.csmBase < 0 || u_shadow.csmCount <= 0) return 1.0;

    float vd = dot(worldPos - cameraPos, u_shadow.camForward.xyz);
    int   ci = u_shadow.csmCount - 1;
    for (int i = 0; i < u_shadow.csmCount; ++i) {
        if (vd <= u_shadow.cascadeSplits[i]) { ci = i; break; }
    }
    return sample2DSlot(u_shadow.csmBase + ci, worldPos, N, ndotl);
}

// Point light: compare normalised distance-to-light against the cube depth.
float sampleCube(int slot, vec3 worldPos, float ndotl) {
    ShadowCube sc = u_shadow.scube[slot];
    vec3  toFrag = worldPos - sc.posRange.xyz;
    float dist   = length(toFrag) / sc.posRange.w;
    if (dist > 1.0) return 1.0;
    float bias   = max(sc.params.x * (1.0 - ndotl), sc.params.x * 0.2) * 2.0;
    float stored = texture(u_shadowCube[slot], toFrag).r;
    return (dist - bias > stored) ? 0.0 : 1.0;
}
