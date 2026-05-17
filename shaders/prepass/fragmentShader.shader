/**
 * Depth/normal prepass fragment shader - thin view-space G-buffer.
 *
 * MRT 0 = view-space normal (rgb) + roughness (a).
 * MRT 1 = view-space position (rgb) + metalness (a).
 *
 * Background is the cleared (0,0,0) position - consumers test xyz, not .w,
 * so the alpha channels are free to carry material data for per-material SSR.
 * Roughness/metalness come from the material UBO scalars (texture-driven
 * variants are a later refinement).
 */
#version 420 core

in vec3 vViewPos;
in vec3 vViewNormal;

layout (location = 0) out vec4 oNormal;
layout (location = 1) out vec4 oPosition;

// Must match MaterialUBOData (gl_material.h) std140 layout, binding 0.
layout(std140, binding = 0) uniform MaterialBlock {
    vec4  albedo;
    vec3  emission;             float _mp0;
    float metallic;
    float roughness;
    float ior;
    float transmission;
    float alpha;
    float ao;
    float clearcoat;
    float clearcoatRoughness;
    float anisotropy;           float _mp1; float _mp2; float _mp3;
    vec3  anisotropyDirection;  float _mp4;
    float subsurface;           float _mp5; float _mp6; float _mp7;
    vec3  subsurfaceColor;      float _mp8;
    float heightScale;
    float normalScale;
    int   textureFlags;
    float _mp9;
} u_material;

void main() {
    oNormal   = vec4(normalize(vViewNormal), clamp(u_material.roughness, 0.0, 1.0));
    oPosition = vec4(vViewPos,               clamp(u_material.metallic,  0.0, 1.0));
}
