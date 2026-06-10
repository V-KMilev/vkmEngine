#version 430 core

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

vec2 signNotZero(vec2 v) {
    return vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

// Octahedral-encode a unit vector to [0,1]^2 (two channels, good precision).
vec2 octEncode(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    vec2 p = n.xy;
    if (n.z < 0.0) p = (1.0 - abs(p.yx)) * signNotZero(p);
    return p * 0.5 + 0.5;
}

void main() {
    if (u_material.type == MAT_ALPHA_MASK) {
        float a = u_material.albedo.a;
        if ((u_material.textureFlags & TEX_ALBEDO) != 0) a *= texture(u_albedoTexture, vUV).a;
        if (a < u_material.alphaCutoff) discard;
    }

    vec2 oct = octEncode(normalize(vViewNormal));
    gbuffer = vec4(oct, u_material.roughness, u_material.metallic);
}
