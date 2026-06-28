#pragma once

#include <string>
#include <optional>

#include <glm/glm.hpp>

#include "resource/asset/material_asset.h"
#include "resource/resource_handle.h"

namespace Engine {

class ResourceManager;

/**
 * @brief Descriptor for loading a PBR material from individual texture files.
 *
 * Allows fine-grained control over which textures to load and their properties.
 * Any texture path left empty will use a generated fallback texture.
 */
struct MaterialLoadDesc {
    // Texture file paths (leave empty to use fallback)
    std::string albedoPath;
    std::string normalPath;
    std::string metallicRoughnessPath;  ///< Packed map: B = metallic, G = roughness
    std::string metallicPath;
    std::string roughnessPath;
    std::string aoPath;
    std::string emissionPath;
    std::string heightPath;             ///< Height/displacement map for parallax

    // Material base properties (used if no texture is provided or as tint)
    glm::vec4 albedo = glm::vec4(1.0f);
    glm::vec3 emission = glm::vec3(0.0f);
    float metallic = 0.0f;
    float roughness = 1.0f;
    float ao = 1.0f;
    float normalScale = 1.0f;
    float heightScale = 0.0f;  ///< Parallax depth scale (disabled by default for safety)

    bool generateMipmaps = true;
};

/**
 * @brief Load a PBR material from a texture folder with automatic file detection.
 *
 * Searches for common PBR texture naming patterns in the specified folder:
 * - Color/Albedo/BaseColor/Diffuse (sRGB)
 * - Normal/NormalGL (linear)
 * - Roughness (linear)
 * - Metallic/Metalness (linear)
 * - AO/AmbientOcclusion/Occlusion (linear)
 * - Emission/Emissive (sRGB)
 * - MetallicRoughness/ORM (linear, packed)
 *
 * Common file extensions are checked: .jpg, .jpeg, .png, .tga, .bmp
 *
 * @param folderPath Path to folder containing PBR textures
 * @param resourceManager Resource manager to add the material to
 * @param baseProperties Optional base material properties (defaults if not provided)
 * @return Handle to the loaded material
 */
MaterialHandle loadMaterialFromFolder(
    const std::string& folderPath,
    ResourceManager& resourceManager,
    const MaterialLoadDesc& baseProperties = MaterialLoadDesc{}
);

} // namespace Engine
