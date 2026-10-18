/*
 * A surface as its material describes it at a uv: the maps' samplers, and what sampleSurface()
 * reads from them and the material block.
 */

#include "material.glsl"
#include "brdf.glsl"

layout(binding = MATERIAL_SLOT_ALBEDO) uniform sampler2D u_albedoTexture;
layout(binding = MATERIAL_SLOT_NORMAL) uniform sampler2D u_normalTexture;
layout(binding = MATERIAL_SLOT_METALLIC_ROUGHNESS) uniform sampler2D u_metallicRoughnessTexture;
layout(binding = MATERIAL_SLOT_AO) uniform sampler2D u_aoTexture;
layout(binding = MATERIAL_SLOT_EMISSION) uniform sampler2D u_emissionTexture;
layout(binding = MATERIAL_SLOT_HEIGHT) uniform sampler2D u_heightTexture;
layout(binding = MATERIAL_SLOT_AO_METALLIC_ROUGHNESS) uniform sampler2D u_aoMetallicRoughnessTexture;
#ifdef LAYERED
// A layered material's layers, a layer of each array apiece, on the units of the maps a layered
// material has no use for; and each layer's share.
layout(binding = MATERIAL_SLOT_CLEARCOAT) uniform sampler2DArray u_layerAlbedo;
layout(binding = MATERIAL_SLOT_TRANSMISSION) uniform sampler2DArray u_layerNormal;
layout(binding = MATERIAL_SLOT_METALLIC) uniform sampler2DArray u_layerSurface;  // ao, roughness, metallic
layout(binding = MATERIAL_SLOT_ROUGHNESS) uniform sampler2D u_layerWeights;
#else
layout(binding = MATERIAL_SLOT_CLEARCOAT) uniform sampler2D u_clearcoatTexture;
layout(binding = MATERIAL_SLOT_TRANSMISSION) uniform sampler2D u_transmissionTexture;
layout(binding = MATERIAL_SLOT_METALLIC) uniform sampler2D u_metallicTexture;
layout(binding = MATERIAL_SLOT_ROUGHNESS) uniform sampler2D u_roughnessTexture;
#endif

// The material's opacity at @p uv: its alpha times the albedo map's.
float sampledOpacity(vec2 uv) {
    return u_material.albedo.a * (hasTex(MATERIAL_SLOT_ALBEDO) ? texture(u_albedoTexture, uv).a : 1.0);
}

struct Surface {
    vec3  albedo;
    float opacity;
    float metallic;
    float roughness;
    float ao;
    vec3  emission;
    // Lobe masks, sampled once per fragment for the per-light lobes.
    float clearcoat;
    float transmission;
    float clearcoatRoughness;  // clamped as the base's is
};

#ifdef LAYERED
// Each layer's share at @p uv, summing to one.
vec4 layerShares(vec2 uv) {
    vec4 w = texture(u_layerWeights, uv);
    return w / max(dot(w, vec4(1.0)), 1e-4);
}

// Where layer @p i reads its maps at the mesh's @p uv.
vec3 layerAt(vec2 uv, int i) {
    return vec3(uv * u_material.layerUV[i].xy + u_material.layerUV[i].zw, float(i));
}

// The layers' tangent-space normal at @p uv, each at its own scale, blended by share.
vec2 layeredNormalXY(vec2 uv) {
    vec4 w  = layerShares(uv);
    vec2 xy = vec2(0.0);
    for (int i = 0; i < MATERIAL_MAX_LAYERS; ++i) {
        xy += w[i] * (texture(u_layerNormal, layerAt(uv, i)).rg * 2.0 - 1.0) * u_material.layerTint[i].w;
    }
    return xy;
}

// How much larger a layer's second, far read repeats; not a simple ratio, so the two repeats never
// line up.
const float LAYER_MACRO_SCALE = 0.137;

// Layer @p i's colour at @p at. Where its maps repeat often enough on screen to show their tiling,
// they are read again at a far larger scale and blended in, so a far hillside is no grid of one tile.
vec3 layerAlbedo(vec3 at, int i) {
    vec2  footprint = fwidth(at.xy);
    float far       = 0.5 * smoothstep(0.004, 0.04, max(footprint.x, footprint.y));
    vec3  near      = texture(u_layerAlbedo, at).rgb;
    vec3  macro     = texture(u_layerAlbedo, vec3(at.xy * LAYER_MACRO_SCALE + vec2(0.31, 0.57), at.z)).rgb;
    return mix(near, macro, far) * u_material.layerTint[i].rgb;
}

// Every layer is read at every fragment: a read skipped where its share is zero would be in
// control flow that differs between neighbours, where the mip a read takes is undefined.
Surface sampleSurface(vec2 uv) {
    vec4    w = layerShares(uv);
    Surface s;
    s.albedo    = vec3(0.0);
    s.ao        = 0.0;
    s.roughness = 0.0;
    s.metallic  = 0.0;
    for (int i = 0; i < MATERIAL_MAX_LAYERS; ++i) {
        vec3 at      = layerAt(uv, i);
        vec3 surface = texture(u_layerSurface, at).rgb * u_material.layerSurface[i].xyz;
        s.albedo    += w[i] * layerAlbedo(at, i);
        s.ao        += w[i] * surface.r;
        s.roughness += w[i] * surface.g;
        s.metallic  += w[i] * surface.b;
    }
    s.opacity            = 1.0;
    s.emission           = vec3(0.0);
    s.clearcoat          = 0.0;
    s.transmission       = 0.0;
    s.roughness          = clamp(s.roughness, MIN_ROUGHNESS, 1.0);
    s.clearcoatRoughness = 1.0;
    return s;
}
#else
Surface sampleSurface(vec2 uv) {
    Surface s;

    s.albedo  = u_material.albedo.rgb;
    s.opacity = u_material.albedo.a;
    if (hasTex(MATERIAL_SLOT_ALBEDO)) {
        vec4 tex = texture(u_albedoTexture, uv);
        s.albedo  *= tex.rgb;
        s.opacity *= tex.a;
    }

    s.metallic  = u_material.metallic;
    s.roughness = u_material.roughness;
    s.ao        = u_material.ao;
    s.emission  = u_material.emission.rgb * u_material.emission.a;

    // Combined maps win over separate ones when both are present.
    if (hasTex(MATERIAL_SLOT_AO_METALLIC_ROUGHNESS)) {
        vec3 amr = texture(u_aoMetallicRoughnessTexture, uv).rgb;
        s.ao        *= amr.r;
        s.roughness *= amr.g;
        s.metallic  *= amr.b;
    } else if (hasTex(MATERIAL_SLOT_METALLIC_ROUGHNESS)) {
        vec3 mr = texture(u_metallicRoughnessTexture, uv).rgb;
        s.roughness *= mr.g;  // glTF: green = roughness
        s.metallic  *= mr.b;  // glTF: blue  = metallic
    } else {
        if (hasTex(MATERIAL_SLOT_METALLIC))  s.metallic  *= texture(u_metallicTexture,  uv).r;
        if (hasTex(MATERIAL_SLOT_ROUGHNESS)) s.roughness *= texture(u_roughnessTexture, uv).r;
    }

    if (hasTex(MATERIAL_SLOT_AO) && !hasTex(MATERIAL_SLOT_AO_METALLIC_ROUGHNESS)) {
        s.ao *= texture(u_aoTexture, uv).r;
    }
    if (hasTex(MATERIAL_SLOT_EMISSION)) {
        s.emission *= texture(u_emissionTexture, uv).rgb;
    }

    s.clearcoat = u_material.clearcoat;
    if (hasTex(MATERIAL_SLOT_CLEARCOAT)) s.clearcoat *= texture(u_clearcoatTexture, uv).r;

    s.transmission = u_material.transmission;
    if (hasTex(MATERIAL_SLOT_TRANSMISSION)) s.transmission *= texture(u_transmissionTexture, uv).r;

    s.roughness          = clamp(s.roughness, MIN_ROUGHNESS, 1.0);
    s.clearcoatRoughness = clamp(u_material.clearcoatRoughness, MIN_ROUGHNESS, 1.0);
    return s;
}
#endif
