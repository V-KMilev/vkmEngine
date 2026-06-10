#version 430 core

/*
 * Depth prepass fragment stage. Opaque / unlit write depth only (empty body).
 * Alpha-masked materials must punch the same holes the forward pass will, or
 * the forward fragments behind a hole would fail the LEQUAL test and show
 * through. Everything else here is depth-only.
 */

#define MAT_ALPHA_MASK 3
#define TEX_ALBEDO     (1 << 0)

in vec2 vUV;

// Matches MaterialBlock in shaders/forward/pbr (std140) - only type / cutoff /
// textureFlags / albedo.a are read, but the full layout is declared so the
// offsets line up with the bound material UBO.
layout(std140, binding = 0) uniform MaterialBlock {
    vec4 albedo;
    vec4 emission;
    vec4 anisotropyDirection;
    vec4 sheenColor;
    vec4 subsurfaceColor;
    vec4 attenuationColor;

    float metallic;
    float roughness;
    float ior;
    float ao;
    float normalScale;
    float clearcoat;
    float clearcoatRoughness;
    float anisotropy;
    float subsurface;
    float transmission;
    float thicknessFactor;
    float heightScale;
    float alphaCutoff;
    int   type;
    int   textureFlags;
    int   _mp0;
} u_material;

layout(binding = 0) uniform sampler2D u_albedoTexture;

void main() {
    if (u_material.type == MAT_ALPHA_MASK) {
        float a = u_material.albedo.a;
        if ((u_material.textureFlags & TEX_ALBEDO) != 0) a *= texture(u_albedoTexture, vUV).a;
        if (a < u_material.alphaCutoff) discard;
    }
}
