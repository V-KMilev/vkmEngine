/*
 * A surface as its material describes it at a uv: the maps' samplers, and what sampleSurface()
 * reads from them and the material block. Needs material.glsl and brdf.glsl, which it includes.
 */

#include "material.glsl"
#include "brdf.glsl"

layout(binding = MATERIAL_SLOT_ALBEDO) uniform sampler2D u_albedoTexture;
layout(binding = MATERIAL_SLOT_NORMAL) uniform sampler2D u_normalTexture;
layout(binding = MATERIAL_SLOT_METALLIC_ROUGHNESS) uniform sampler2D u_metallicRoughnessTexture;
layout(binding = MATERIAL_SLOT_AO) uniform sampler2D u_aoTexture;
layout(binding = MATERIAL_SLOT_EMISSION) uniform sampler2D u_emissionTexture;
layout(binding = MATERIAL_SLOT_HEIGHT) uniform sampler2D u_heightTexture;
layout(binding = MATERIAL_SLOT_CLEARCOAT) uniform sampler2D u_clearcoatTexture;
layout(binding = MATERIAL_SLOT_TRANSMISSION) uniform sampler2D u_transmissionTexture;
layout(binding = MATERIAL_SLOT_METALLIC) uniform sampler2D u_metallicTexture;
layout(binding = MATERIAL_SLOT_ROUGHNESS) uniform sampler2D u_roughnessTexture;
layout(binding = MATERIAL_SLOT_AO_METALLIC_ROUGHNESS) uniform sampler2D u_aoMetallicRoughnessTexture;

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
