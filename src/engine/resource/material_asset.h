#pragma once

#include <glm/glm.hpp>

#include "resource/resource.h"
#include "resource/resource_handle.h"

#include "resource/texture_asset.h"

namespace Engine {

/**
 * @brief Describes a material asset.
 *
 * Holds PBR material scalar properties and references to associated textures.
 * Supports both rasterization and raytracing rendering pipelines.
 */
struct MaterialAsset : public Resource {
    // Base PBR properties
    glm::vec4 albedo   = {1,1,1,1};              ///< Base albedo color (RGBA)
    glm::vec3 emission = {0,0,0};                ///< Emissive color (RGB)
    float metallic     = 0.0f;                   ///< Metalness (0: dielectric, 1: metallic)
    float roughness    = 0.5f;                   ///< Surface roughness (0: smooth, 1: rough)

    // Essential for raytracing and advanced PBR
    float ior          = 1.5f;                   ///< Index of refraction (1.0: air, 1.5: glass, 2.4: diamond)
    float transmission = 0.0f;                   ///< Transmission factor (0: opaque, 1: fully transparent)
    float alpha        = 1.0f;                   ///< Alpha/opacity (0: transparent, 1: opaque)
    float ao           = 1.0f;                   ///< Ambient occlusion factor (0: fully occluded, 1: no occlusion)

    // Advanced PBR properties
    float clearcoat               = 0.0f;        ///< Clearcoat layer strength (0: none, 1: full)
    float clearcoatRoughness      = 0.0f;        ///< Clearcoat roughness (0: smooth, 1: rough)
    float anisotropy              = 0.0f;        ///< Anisotropy strength (0: isotropic, 1: fully anisotropic)
    glm::vec3 anisotropyDirection = {1,0,0};     ///< Anisotropy direction (tangent space)

    // Subsurface scattering (for skin, wax, etc.)
    float subsurface          = 0.0f;            ///< Subsurface scattering strength
    glm::vec3 subsurfaceColor = {1,1,1};         ///< Subsurface color tint

    // Height/Displacement and Normal mapping
    float heightScale = 0.0f;                   ///< Height map scale for parallax/displacement mapping (0.02-0.1 typical)
    float normalScale = 1.0f;                   ///< Normal map intensity (0: flat, 1: normal, >1: exaggerated)

    // Texture handles
    TextureHandle albedoTexture;                 ///< Albedo (base color) texture (RGBA)
    TextureHandle emissionTexture;               ///< Emission (self-illumination) texture (RGB or RGBA)
    TextureHandle roughnessTexture;              ///< Surface roughness texture (G or R channel)
    TextureHandle metallicTexture;               ///< Surface metalness texture (B or R channel)
    TextureHandle normalTexture;                 ///< Tangent-space normal map texture (RGB or RGBA)
    TextureHandle aoTexture;                     ///< Ambient occlusion texture (R channel)
    TextureHandle heightTexture;                 ///< Height/displacement map texture (R channel)
    TextureHandle clearcoatTexture;              ///< Clearcoat mask texture (R channel: clearcoat strength)
    TextureHandle transmissionTexture;           ///< Transmission mask texture (A or R channel: transparency/IOR)

    // Combined texture maps (common optimization)
    TextureHandle metallicRoughnessTexture;      ///< Combined metallic (R) + roughness (G) texture
    TextureHandle aoMetallicRoughnessTexture;    ///< Combined AO (R) + metallic (G) + roughness (B)
};

using MaterialHandle = Handle<MaterialAsset>;

} // namespace Engine
