#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "resource/resource.h"
#include "resource/resource_handle.h"

#include "resource/texture_asset.h"

namespace Engine {

/**
 * @brief How the renderer draws a material - render path, not shading.
 *
 * Mirrors glTF alphaMode plus Unlit: Opaque and AlphaMask write depth and
 * draw in the opaque bucket (AlphaMask discards below alphaCutoff);
 * Transparent draws in the sorted blended bucket; Unlit skips the BRDF and
 * outputs albedo + emission directly.
 */
enum class MaterialType : uint8_t {
    Opaque      = 0,
    Transparent = 1,
    Unlit       = 2,
    AlphaMask   = 3
};

/// Names in MaterialType order - the single source for JSON (de)serialization
/// and the editor's type combo, so the two cannot drift out of enum order.
inline constexpr const char* const MATERIAL_TYPE_NAMES[] = {
    "Opaque", "Transparent", "Unlit", "AlphaMask"
};

/**
 * @brief Bitset of optional PBR lobes / features a material actually uses.
 *
 * Used by the shader variant cache: the engine compiles one PBR program
 * per distinct flag set so a plain opaque-diffuse material doesn't pay the
 * fragment cost of branches for clearcoat / transmission / sheen / etc.
 * MaterialAsset::featureFlags() derives the bitset from the current scalar
 * values and texture presence. Dormant until the variant cache returns -
 * the ubershader branches at runtime for now.
 */
enum class MaterialFeature : uint32_t {
    None         = 0,
    Transmission = 1u << 0,  ///< transmission > 0 (refracts the scene behind)
    Volume       = 1u << 1,  ///< thicknessFactor > 0 (Beer-Lambert through the medium)
    Clearcoat    = 1u << 2,  ///< clearcoat > 0
    Anisotropy   = 1u << 3,  ///< anisotropy > 0
    Subsurface   = 1u << 4,  ///< subsurface > 0
    Sheen        = 1u << 5,  ///< sheenColor != 0
    Parallax     = 1u << 6,  ///< heightTexture + heightScale > 0
    AlphaMask    = 1u << 7,  ///< alphaCutoff > 0 (discard path)
};

constexpr uint32_t toBits(MaterialFeature f) { return static_cast<uint32_t>(f); }

/// All features OR'd together. The default ubershader path uses this so the
/// shader compiles every optional block; per-material variants narrow it.
constexpr uint32_t MATERIAL_ALL_FEATURES =
    toBits(MaterialFeature::Transmission) |
    toBits(MaterialFeature::Volume)       |
    toBits(MaterialFeature::Clearcoat)    |
    toBits(MaterialFeature::Anisotropy)   |
    toBits(MaterialFeature::Subsurface)   |
    toBits(MaterialFeature::Sheen)        |
    toBits(MaterialFeature::Parallax)     |
    toBits(MaterialFeature::AlphaMask);

/**
 * @brief A complete PBR material.
 *
 * The parameter set follows the Disney/glTF principled model: a metal-rough
 * microfacet base layer, the standard secondary lobes (clearcoat, sheen,
 * anisotropy, subsurface), transmission + volume for light through the
 * surface, and per-map texture handles where every scalar acts as a
 * multiplier on its map when one is bound. This asset is the spec the
 * renderer implements - fields here and shader support move in lockstep.
 */
struct MaterialAsset : public Resource {
    // Render path
    MaterialType type = MaterialType::Opaque;
    bool  doubleSided = false;                   ///< Disable backface culling and flip the normal on back faces
    float alphaCutoff = 0.5f;                    ///< AlphaMask: discard below this albedo alpha (glTF default 0.5)

    // Base layer - the microfacet core
    glm::vec4 albedo   = {1,1,1,1};              ///< Base color (RGB) + opacity (A; Transparent type blends on it)
    float metallic     = 0.0f;                   ///< Metalness (0: dielectric, 1: metallic)
    float roughness    = 0.5f;                   ///< Surface roughness (0: smooth, 1: rough); GGX alpha = roughness^2
    float ior          = 1.5f;                   ///< Index of refraction; dielectric F0 = ((ior-1)/(ior+1))^2
    float ao           = 1.0f;                   ///< Ambient occlusion factor on indirect light (0: occluded, 1: open)
    float normalScale  = 1.0f;                   ///< Normal map intensity (0: flat, 1: as authored, >1: exaggerated)

    glm::vec3 emission     = {0,0,0};            ///< Emissive color (RGB), linear
    float emissiveStrength = 1.0f;               ///< HDR multiplier on emission (drives bloom once it returns)

    // Secondary lobes
    float clearcoat               = 0.0f;        ///< Clearcoat layer strength (0: none, 1: full); attenuates the base layer
    float clearcoatRoughness      = 0.0f;        ///< Clearcoat lobe roughness (0: smooth, 1: rough)
    float anisotropy              = 0.0f;        ///< Anisotropy strength (0: isotropic, 1: fully anisotropic)
    glm::vec3 anisotropyDirection = {1,0,0};     ///< Anisotropy direction (tangent space)

    glm::vec3 sheenColor     = {0,0,0};          ///< Sheen tint, Charlie lobe (0 = disabled); cloth / velvet
    float     sheenRoughness = 0.3f;             ///< Sheen lobe roughness

    float subsurface          = 0.0f;            ///< Subsurface scattering strength; skin / wax / leaves
    glm::vec3 subsurfaceColor = {1,1,1};         ///< Subsurface color tint

    // Transmission + volume - light through the surface
    float transmission = 0.0f;                   ///< Fraction of light refracted instead of diffused (0: opaque, 1: glass)
    // KHR_materials_volume - Beer-Lambert absorption inside a transmissive
    // medium. thicknessFactor == 0 means "thin-walled" and absorption is
    // disabled (matches the glTF spec default). attenuationColor is the
    // color that white light turns into after travelling attenuationDistance
    // through the volume; transmittance per channel = pow(c, t / d).
    float     thicknessFactor     = 0.0f;        ///< Volume thickness in metres (0: thin-walled, no absorption)
    float     attenuationDistance = 1.0f;        ///< Path length at which radiance reaches attenuationColor (m)
    glm::vec3 attenuationColor    = {1,1,1};     ///< Transmittance after one attenuationDistance (white = no tint)

    // Geometry detail
    float heightScale = 0.0f;                    ///< Parallax-occlusion depth scale (0: off; 0.02-0.1 typical)

    // Texture maps - each multiplies its scalar/color factor when bound
    TextureHandle albedoTexture;                 ///< Base color (RGBA; A feeds AlphaMask / Transparent)
    TextureHandle normalTexture;                 ///< Tangent-space normal map (RGB)
    TextureHandle metallicRoughnessTexture;      ///< Combined roughness (G) + metallic (B), glTF layout
    TextureHandle metallicTexture;               ///< Separate metalness (R channel)
    TextureHandle roughnessTexture;              ///< Separate roughness (R channel)
    TextureHandle aoTexture;                     ///< Ambient occlusion (R channel)
    TextureHandle aoMetallicRoughnessTexture;    ///< Combined AO (R) + roughness (G) + metallic (B)
    TextureHandle emissionTexture;               ///< Emission (RGB)
    TextureHandle heightTexture;                 ///< Height field for parallax (R channel)
    TextureHandle clearcoatTexture;              ///< Clearcoat strength mask (R channel)
    TextureHandle transmissionTexture;           ///< Transmission mask (R channel)

    /**
     * @brief Compute the MaterialFeature bitset this material requires.
     *
     * Drives shader variant selection. Thresholds match the runtime guards
     * in the PBR fragment shader so a feature that's optimised out at
     * compile time also wasn't running its `if (X > 0.001)` block.
     *
     * Pure function over the asset's current state; called whenever a
     * material is (re)synced to the GPU, so live edits in the Material
     * Editor are picked up.
     */
    uint32_t featureFlags() const {
        uint32_t f = 0;
        if (transmission > 0.001f)                  f |= toBits(MaterialFeature::Transmission);
        if (thicknessFactor > 0.0f)                 f |= toBits(MaterialFeature::Volume);
        if (clearcoat > 0.001f)                     f |= toBits(MaterialFeature::Clearcoat);
        if (anisotropy > 0.001f)                    f |= toBits(MaterialFeature::Anisotropy);
        if (subsurface > 0.001f)                    f |= toBits(MaterialFeature::Subsurface);
        if (sheenColor.x > 0.0f ||
            sheenColor.y > 0.0f ||
            sheenColor.z > 0.0f)                    f |= toBits(MaterialFeature::Sheen);
        if (heightTexture && heightScale > 0.0f)    f |= toBits(MaterialFeature::Parallax);
        if (type == MaterialType::AlphaMask)        f |= toBits(MaterialFeature::AlphaMask);
        return f;
    }
};

using MaterialHandle = Handle<MaterialAsset>;

} // namespace Engine
