#pragma once

#include <string>

#include "resource/asset/material_asset.h"
#include "resource/resource_handle.h"

namespace Engine {

class ResourceManager;

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
 * The folder path is the material's name, and the load is idempotent by it:
 * loading the same folder twice hands back the material from the first load.
 *
 * @param folderPath Path to folder containing PBR textures
 * @param resourceManager Resource manager to add the material to
 * @return Handle to the loaded material, or an invalid handle if the folder is missing
 */
MaterialHandle loadMaterialFromFolder(
    const std::string& folderPath,
    ResourceManager& resourceManager
);

} // namespace Engine
