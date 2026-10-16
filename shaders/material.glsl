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
    vec4 uvTransform;          // uvScale in xy, uvOffset in zw

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
    int   waves;  // 1: the scene's ocean moves the surface (ocean.glsl)
    float boxMapping;  // metres a repeat when box-mapped; 0 reads the mesh's UVs
    float pad0;
    float pad1;
    float pad2;

    // A layered material's layers (MaterialAsset::layers), read by the LAYERED programs alone.
    vec4 layerTint[MATERIAL_MAX_LAYERS];     // albedo factor; w the normal scale
    vec4 layerUV[MATERIAL_MAX_LAYERS];       // uvScale in xy, uvOffset in zw
    vec4 layerSurface[MATERIAL_MAX_LAYERS];  // ao, roughness and metallic factors
} u_material;

// Where a mesh's uv reads the material's maps: repeated by uvScale, slid by uvOffset.
vec2 materialUV(vec2 uv) {
    return uv * u_material.uvTransform.xy + u_material.uvTransform.zw;
}

// A box-mapped uv (MaterialAsset::boxMapping): off where @p pos lies in the object's frame, scaled to
// the size @p model draws it, on the face @p normal (the object's) most nearly turns to as drawn -
// a normal scales inversely; @p tangent is that face's +u in the object's frame, cross(normal,
// tangent) its +v.
vec2 boxUV(mat4 model, vec3 pos, vec3 normal, out vec3 tangent) {
    const vec3 drawn = vec3(length(model[0].xyz), length(model[1].xyz), length(model[2].xyz));
    const vec3 p = pos * drawn / u_material.boxMapping;
    const vec3 a = abs(normal / max(drawn, vec3(1e-6)));
    if (a.y >= a.x && a.y >= a.z) {
        tangent = vec3(1.0, 0.0, 0.0);
        return normal.y > 0.0 ? vec2(p.x, -p.z) : vec2(p.x, p.z);
    }
    if (a.x >= a.z) {
        tangent = vec3(0.0, 0.0, normal.x > 0.0 ? -1.0 : 1.0);
        return vec2(normal.x > 0.0 ? -p.z : p.z, p.y);
    }
    tangent = vec3(normal.z > 0.0 ? 1.0 : -1.0, 0.0, 0.0);
    return vec2(normal.z > 0.0 ? p.x : -p.x, p.y);
}

// Whether the material binds a map: textureFlags carries one bit per MATERIAL_SLOT_*.
bool hasTex(int slot) {
    return (u_material.textureFlags & (1 << slot)) != 0;
}
