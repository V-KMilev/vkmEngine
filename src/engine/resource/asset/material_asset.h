#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

#include "resource/resource.h"
#include "resource/handle.h"

#include "resource/asset/texture_asset.h"

#include "base/reflect/reflect.h"
#include "base/reflect/enum_names.h"

namespace Vkm::Engine {

/**
 * @brief The material's texture maps, once, as (serialized key, member, texture slot).
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
    Opaque,
    Transparent,
    Unlit,
    AlphaMask,
    Count
};

struct MaterialAsset;
using MaterialHandle = Handle<MaterialAsset>;

/**
 * @brief A complete PBR material.
 *
 * Burley's principled model, as glTF has it; each scalar multiplies its map when one is
 * bound. Fields here and shader support move in lockstep.
 */
struct MaterialAsset : public Resource {
    static constexpr uint32_t MAX_LAYERS = 4;  ///< The most materials a layered one blends (layers).

    MaterialType type        = MaterialType::Opaque;
    float        alphaCutoff = 0.5f;                  ///< AlphaMask: discard below this albedo alpha (glTF
                                                      ///< default 0.5)
    bool         doubleSided = false;                 ///< Both faces drawn, a back face lit as its own (glTF
                                                      ///< doubleSided): cloth, leaves, cards

    glm::vec4 albedo      = {1,1,1,1};  ///< Base color (RGB) + opacity (A; Transparent type blends on it)
    float     metallic    = 0.0f;       ///< Metalness (0: dielectric, 1: metallic)
    float     roughness   = 0.5f;       ///< Surface roughness (0: smooth, 1: rough); GGX alpha = roughness^2
    float     ior         = 1.5f;       ///< Index of refraction; dielectric F0 = ((ior-1)/(ior+1))^2
    float     ao          = 1.0f;       ///< Ambient occlusion (0: occluded, 1: open): indirect light, and
                                        ///< direct light at grazing angles (micro-shadowing)
    float     normalScale = 1.0f;       ///< Normal map intensity (0: flat, 1: as authored, >1: exaggerated)

    glm::vec3 emission         = {0,0,0};  ///< Emissive color (RGB), linear
    float     emissiveStrength = 1.0f;     ///< HDR multiplier on emission

    float clearcoat               = 0.0f;  ///< Clearcoat layer strength (0: none, 1: full); attenuates
                                           ///< the base layer
    float clearcoatRoughness      = 0.0f;  ///< Clearcoat lobe roughness (0: smooth, 1: rough)

    glm::vec3 sheenColor     = {0,0,0};    ///< Sheen tint, Charlie lobe (0 = disabled); cloth / velvet
    float     sheenRoughness = 0.3f;       ///< Sheen lobe roughness

    float     subsurface      = 0.0f;      ///< Subsurface scattering strength; skin / wax / leaves
    glm::vec3 subsurfaceColor = {1,1,1};   ///< Subsurface color tint

    float transmission = 0.0f;  ///< Fraction of light refracted instead of diffused (0: opaque, 1: glass)
    // KHR_materials_volume: transmittance = pow(attenuationColor, path / attenuationDistance),
    // the path from thicknessFactor.
    float     thicknessFactor     = 0.0f;     ///< Volume thickness in meters (0: thin-walled, no absorption)
    float     attenuationDistance = 1.0f;     ///< Path length at which radiance reaches attenuationColor (m)
    glm::vec3 attenuationColor    = {1,1,1};  ///< Transmittance after one attenuationDistance (white = no
                                              ///< tint)

    glm::vec2 uvScale  = {1.0f, 1.0f};  ///< A mesh's maps are read at uv * uvScale + uvOffset: how often they
                                        ///< repeat across its UVs on each axis, and how far they slide; 200
                                        ///< lays a texture two hundred times along a field. A texture repeats
                                        ///< only if its recipe says "wrap": "repeat", as a file's clamps by
                                        ///< default. A glTF's KHR_texture_transform on its base color, else
                                        ///< its normal map, imports as these, without its rotation. A decal,
                                        ///< which lays its maps over its box, and water with waves
                                        ///< (Water::waves) read neither.
    glm::vec2 uvOffset = {0.0f, 0.0f};

    float boxMapping = 0.0f;  ///< Meters one repeat of the maps spans when box-mapped: the uv is read off
                              ///< where a point lies in its object's own frame, at the size it is drawn, on
                              ///< the face its normal most nearly turns to, so a stretched cube is not a
                              ///< stretched texture and the texture moves with the object. 0 reads the mesh's
                              ///< UVs. For boxes and faceted meshes: a smooth one shows a seam where its
                              ///< normal turns from face to face. A skinned mesh keeps its UVs.

#define VKM_MATERIAL_MAP_MEMBER(key, member, slot, doc) TextureHandle member;
    VKM_MATERIAL_MAPS(VKM_MATERIAL_MAP_MEMBER)
#undef VKM_MATERIAL_MAP_MEMBER

    std::array<MaterialHandle, MAX_LAYERS> layers{};      ///< Materials blended in its place by layerWeights'
                                                          ///< R, G, B and A: what a terrain draws with. Each
                                                          ///< layer's albedo, normal and
                                                          ///< AO/roughness/metallic maps and factors are read
                                                          ///< at its own tiling, and again at a far larger
                                                          ///< scale where it would repeat densely on screen,
                                                          ///< so a far slope shows no grid; no other field
                                                          ///< here is read. Derived, not saved: the terrain
                                                          ///< makes and owns it (TerrainSystem).
    TextureHandle                          layerWeights;  ///< Each layer's share, read at the mesh's uv.
    std::array<glm::vec2, MAX_LAYERS> layerTiling{};      ///< Each layer's repeats across the mesh's uv, read
                                                          ///< in place of the layer's own uvScale, zero for
                                                          ///< once: a terrain tiles each layer at its own
                                                          ///< size in meters.

    /// Whether it blends layers, as a terrain's does.
    bool layered() const {
        for (const MaterialHandle& layer : layers) {
            if (layer) return true;
        }
        return false;
    }
};

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::MaterialType, "Opaque", "Transparent", "Unlit", "AlphaMask")

// Non-texture fields; texture handles serialize separately as name refs.
VKM_REFLECT_BEGIN(::Vkm::Engine::MaterialAsset)
    VKM_F(type)
    VKM_F(alphaCutoff, 0.0f, 1.0f, "Fragments with albedo alpha below it are dropped: foliage, leaves")
    VKM_F(doubleSided, "A back face draws too, lit as its own: cloth, leaves, single-plane cards")
    VKM_F(albedo)
    VKM_F(metallic, 0.0f, 1.0f, "Metalness, multiplying its map's red channel")
    VKM_F(roughness, 0.0f, 1.0f, "Roughness, multiplying its map's red channel")
    VKM_F(ior, 1.0f, 3.0f, "How strongly it reflects head-on: 1.0 air, 1.33 water, 1.5 glass, 2.4 diamond")
    VKM_F(ao, 0.0f, 1.0f, "Scales indirect light over the whole material")
    VKM_F(normalScale, 0.0f, 5.0f, "How far the normal map bends the surface")
    VKM_F(emission, "Light the material gives off on its own")
    VKM_F(emissiveStrength, 0.0f, 64.0f, "HDR multiplier on the emission color; this is what drives bloom")
    VKM_F(clearcoat, 0.0f, 1.0f, "How much of a gloss layer lies over the base")
    VKM_F(clearcoatRoughness, 0.0f, 1.0f, "Roughness of the coat alone; the layer under it keeps its own")
    VKM_F(sheenColor, "Black is no sheen at all, which is how this feature switches off")
    VKM_F(sheenRoughness, 0.0f, 1.0f, "How far round from the rim the sheen spreads")
    VKM_F(subsurface, 0.0f, 1.0f, "How much light enters and leaves somewhere else")
    VKM_F(subsurfaceColor, "Color light picks up on its way back out")
    VKM_F(transmission, 0.0f, 1.0f, "How much light refracts through instead of scattering off")
    VKM_F(thicknessFactor, 0.0f, 100.0f, "Volume thickness, meters; 0 is thin-walled, and absorbs nothing")
    VKM_F(attenuationDistance, 0.0001f, 1000.0f, "Meters in which white light turns the absorption color")
    VKM_F(attenuationColor, "What white light turns into over one absorption distance (white = clear)")
    VKM_F(uvScale, -10000.0f, 10000.0f, "Repeats of the maps across the UVs, per axis; the texture must wrap")
    VKM_F(uvOffset, -10000.0f, 10000.0f, "How far the maps slide, in repeats")
    VKM_F(boxMapping, 0.0f, 1000.0f, "Meters one repeat spans off the object's drawn box; 0 reads the UVs")
VKM_REFLECT_END()
