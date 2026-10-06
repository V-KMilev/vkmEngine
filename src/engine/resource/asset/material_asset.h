#pragma once

#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

#include "resource/resource.h"
#include "resource/resource_handle.h"

#include "resource/asset/texture_asset.h"

#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief The material's texture maps, once, as (serialized key, member, GL slot).
 *
 * A table with editorial order or wording does not expand this one; it asserts
 * against MATERIAL_MAP_COUNT instead. The fourth column documents each map and
 * no expansion uses it.
 */
#define VKM_MATERIAL_MAPS(X)                                                                                                            \
    X(albedo,              albedoTexture,              ALBEDO,                Base color (RGBA; A feeds AlphaMask / Transparent))       \
    X(normal,              normalTexture,              NORMAL,                Tangent-space normal map (RGB))                           \
    X(metallicRoughness,   metallicRoughnessTexture,   METALLIC_ROUGHNESS,    Combined roughness (G) + metallic (B) in the glTF layout) \
    X(metallic,            metallicTexture,            METALLIC,              Separate metalness (R channel))                           \
    X(roughness,           roughnessTexture,           ROUGHNESS,             Separate roughness (R channel))                           \
    X(ao,                  aoTexture,                  AO,                    Ambient occlusion (R channel))                            \
    X(aoMetallicRoughness, aoMetallicRoughnessTexture, AO_METALLIC_ROUGHNESS, Combined AO (R) + roughness (G) + metallic (B))           \
    X(emission,            emissionTexture,            EMISSION,              Emission (RGB))                                           \
    X(height,              heightTexture,              HEIGHT,                Height field for parallax (R channel, white is high))     \
    X(clearcoat,           clearcoatTexture,           CLEARCOAT,             Clearcoat strength mask (R channel))                      \
    X(transmission,        transmissionTexture,        TRANSMISSION,          Transmission mask (R channel))

/**
 * @brief How many maps that list holds.
 *
 * Lets a hand-written table of the maps assert it is still complete.
 */
#define VKM_MATERIAL_MAP_COUNT(key, member, slot, doc) +1
inline constexpr std::size_t MATERIAL_MAP_COUNT = 0 VKM_MATERIAL_MAPS(VKM_MATERIAL_MAP_COUNT);
#undef VKM_MATERIAL_MAP_COUNT

/**
 * @brief How the renderer draws a material - render path, not shading.
 *
 * glTF alphaMode plus Unlit: Opaque and AlphaMask draw in the opaque bucket
 * (AlphaMask discards below alphaCutoff); Transparent in the sorted blended
 * bucket; Unlit skips the BRDF and outputs albedo + emission.
 */
enum class MaterialType : uint8_t {
    Opaque      = 0,
    Transparent = 1,
    Unlit       = 2,
    AlphaMask   = 3,
    Count
};

/**
 * @brief A complete PBR material.
 *
 * The Disney/glTF principled model; each scalar multiplies its map when one is
 * bound. Fields here and shader support move in lockstep.
 */
struct MaterialAsset : public Resource {
    MaterialType type = MaterialType::Opaque;
    /// AlphaMask: discard below this albedo alpha (glTF default 0.5)
    float alphaCutoff = 0.5f;

    /// Base color (RGB) + opacity (A; Transparent type blends on it)
    glm::vec4 albedo   = {1,1,1,1};
    float metallic     = 0.0f;                   ///< Metalness (0: dielectric, 1: metallic)
    /// Surface roughness (0: smooth, 1: rough); GGX alpha = roughness^2
    float roughness    = 0.5f;
    float ior          = 1.5f;                   ///< Index of refraction; dielectric F0 = ((ior-1)/(ior+1))^2
    /// Ambient occlusion factor on indirect light (0: occluded, 1: open)
    float ao           = 1.0f;
    /// Normal map intensity (0: flat, 1: as authored, >1: exaggerated)
    float normalScale  = 1.0f;

    glm::vec3 emission     = {0,0,0};            ///< Emissive color (RGB), linear
    float emissiveStrength = 1.0f;               ///< HDR multiplier on emission

    /// Clearcoat layer strength (0: none, 1: full); attenuates the base layer
    float clearcoat               = 0.0f;
    float clearcoatRoughness      = 0.0f;        ///< Clearcoat lobe roughness (0: smooth, 1: rough)
    float anisotropy              = 0.0f;        ///< Anisotropy strength (0: isotropic, 1: fully anisotropic)
    glm::vec3 anisotropyDirection = {1,0,0};     ///< Anisotropy direction (tangent space)

    glm::vec3 sheenColor     = {0,0,0};          ///< Sheen tint, Charlie lobe (0 = disabled); cloth / velvet
    float     sheenRoughness = 0.3f;             ///< Sheen lobe roughness

    float subsurface          = 0.0f;            ///< Subsurface scattering strength; skin / wax / leaves
    glm::vec3 subsurfaceColor = {1,1,1};         ///< Subsurface color tint

    /// Fraction of light refracted instead of diffused (0: opaque, 1: glass)
    float transmission = 0.0f;
    // KHR_materials_volume: transmittance = pow(attenuationColor, path / attenuationDistance),
    // the path from thicknessFactor.
    /// Volume thickness in metres (0: thin-walled, no absorption)
    float     thicknessFactor     = 0.0f;
    /// Path length at which radiance reaches attenuationColor (m)
    float     attenuationDistance = 1.0f;
    /// Transmittance after one attenuationDistance (white = no tint)
    glm::vec3 attenuationColor    = {1,1,1};

    /// Parallax-occlusion depth scale (0: off; 0.02-0.1 typical)
    float heightScale = 0.0f;

#define VKM_MATERIAL_MAP_MEMBER(key, member, slot, doc) TextureHandle member;
    VKM_MATERIAL_MAPS(VKM_MATERIAL_MAP_MEMBER)
#undef VKM_MATERIAL_MAP_MEMBER
};

using MaterialHandle = Handle<MaterialAsset>;

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::MaterialType, "Opaque", "Transparent", "Unlit", "AlphaMask")

// Non-texture fields; texture handles serialize separately as name refs.
VKM_REFLECT_BEGIN(::Vkm::Engine::MaterialAsset)
    VKM_F(type)
    VKM_F(alphaCutoff)
    VKM_F(albedo)
    VKM_F(metallic)
    VKM_F(roughness)
    VKM_F(ior)
    VKM_F(ao)
    VKM_F(normalScale)
    VKM_F(emission)
    VKM_F(emissiveStrength)
    VKM_F(clearcoat)
    VKM_F(clearcoatRoughness)
    VKM_F(anisotropy)
    VKM_F(anisotropyDirection)
    VKM_F(sheenColor)
    VKM_F(sheenRoughness)
    VKM_F(subsurface)
    VKM_F(subsurfaceColor)
    VKM_F(transmission)
    VKM_F(thicknessFactor)
    VKM_F(attenuationDistance)
    VKM_F(attenuationColor)
    VKM_F(heightScale)
VKM_REFLECT_END()
