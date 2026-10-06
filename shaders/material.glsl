/*
 * The per-draw material UBO. Matches MaterialUBO (gl_material.h) std140
 * layout exactly: vec4 block first, then the scalar tail in the same order;
 * the binding comes from the shader prelude. Any change to
 * gl_material.h lands here, once.
 */

layout(std140, binding = UBO_MATERIAL) uniform MaterialBlock {
    vec4 albedo;               // rgb + opacity
    vec4 emission;             // rgb + emissiveStrength
    vec4 anisotropyDirection;  // xyz tangent-space direction
    vec4 sheenColor;           // rgb + sheenRoughness
    vec4 subsurfaceColor;      // rgb
    vec4 attenuationColor;     // rgb + attenuationDistance

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

// Whether the material binds a map: textureFlags carries one bit per MATERIAL_SLOT_*.
bool hasTex(int slot) {
    return (u_material.textureFlags & (1 << slot)) != 0;
}
