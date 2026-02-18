#pragma once

#include <string>
#include <optional>

#include <glm/glm.hpp>

#include "resource/material_asset.h"
#include "resource/resource_handle.h"

namespace Engine {
    class ResourceManager;
}

namespace Engine {

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
    std::string metallicRoughnessPath;  // Combined texture (B = metallic, G = roughness)
    std::string metallicPath;           // Separate metallic texture
    std::string roughnessPath;          // Separate roughness texture
    std::string aoPath;
    std::string emissionPath;
    std::string heightPath;             // Height/displacement map for parallax

    // Material base properties (used if no texture is provided or as tint)
    glm::vec4 albedo = glm::vec4(1.0f);
    glm::vec3 emission = glm::vec3(0.0f);
    float metallic = 0.0f;
    float roughness = 1.0f;
    float ao = 1.0f;
    float normalScale = 1.0f;  // Normal map intensity
    float heightScale = 0.0f; // Parallax depth scale (disabled by default for safety)

    // Texture generation options
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
 * 
 * @example
 * auto material = loadMaterialFromFolder(
 *     "assets/materials/brick",
 *     resources
 * );
 */
MaterialHandle loadMaterialFromFolder(
    const std::string& folderPath,
    ResourceManager& resourceManager,
    const MaterialLoadDesc& baseProperties = MaterialLoadDesc{}
);

/**
 * @brief Load a PBR material from a descriptor with explicit texture paths.
 * 
 * Provides fine-grained control over which textures to load.
 * Any texture path left empty will use a generated fallback texture.
 * 
 * @param desc Material load descriptor
 * @param resourceManager Resource manager to add the material to
 * @return Handle to the loaded material
 * 
 * @example
 * MaterialLoadDesc desc;
 * desc.albedoPath = "assets/textures/brick_color.jpg";
 * desc.normalPath = "assets/textures/brick_normal.png";
 * desc.roughness = 0.8f;
 * auto material = loadMaterialFromDesc(desc, resources);
 */
MaterialHandle loadMaterialFromDesc(
    const MaterialLoadDesc& desc,
    ResourceManager& resourceManager
);

/**
 * @brief Quick helper to load a material with just an albedo texture.
 * 
 * Creates a non-metallic material with the specified albedo texture
 * and default properties. Useful for simple materials.
 * 
 * @param albedoPath Path to albedo/color texture
 * @param resourceManager Resource manager to add the material to
 * @param roughness Roughness value (default: 1.0 = fully rough)
 * @param generateMipmaps Whether to generate mipmaps (default: true)
 * @return Handle to the loaded material
 * 
 * @example
 * auto material = loadSimpleMaterial(
 *     "assets/textures/wood_color.jpg",
 *     resources,
 *     0.6f  // slightly glossy
 * );
 */
MaterialHandle loadSimpleMaterial(
    const std::string& albedoPath,
    ResourceManager& resourceManager,
    float roughness = 1.0f,
    bool generateMipmaps = true
);

} // namespace Engine
