/*
 * Depth + G-buffer prepass fragment stage. Writes a view-space normal
 * (octahedral) plus roughness + metalness into colour attachment 1 - the
 * inputs SSR needs - while the depth attachment is primed for the forward
 * pass's early-Z. Alpha-masked materials punch the same holes the forward pass
 * will, or forward fragments behind a hole would fail LEQUAL and show through.
 */

#define MAT_ALPHA_MASK 3
#define TEX_ALBEDO     (1 << 0)

in vec2 vUV;
in vec3 vViewNormal;

out vec4 gbuffer;  // -> COLOR_ATTACHMENT1: oct view-normal.xy, roughness, metalness

// Matches MaterialBlock in shaders/forward/pbr (std140); the full layout is
// declared so the scalar-tail offsets line up with the bound material UBO.
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

#include "../../_common/normal_codec.glsl"  // signNotZero, octEncode

void main() {
    if (u_material.type == MAT_ALPHA_MASK) {
        float a = u_material.albedo.a;
        if ((u_material.textureFlags & TEX_ALBEDO) != 0) a *= texture(u_albedoTexture, vUV).a;
        if (a < u_material.alphaCutoff) discard;
    }

    vec2 oct = octEncode(normalize(vViewNormal));
    gbuffer = vec4(oct, u_material.roughness, u_material.metallic);
}
