#pragma once

#include "resource/asset/material_asset.h"
#include "resource/resource_handle.h"

namespace Engine {

class ResourceManager;

/**
 * @brief Generate a default PBR material.
 *
 * Creates a neutral white PBR material with default properties:
 * - Albedo: white (1,1,1,1)
 * - Roughness: 0.5 (semi-rough)
 * - Metallic: 0.0 (dielectric)
 * - AO: 1.0 (no occlusion)
 * - Emission: black (no emission)
 *
 * Automatically generates default textures (white, black, normal, gray)
 * and assigns them to the material.
 *
 * @param resourceManager Resource manager to create material in.
 * @return Handle to the generated material.
 */
MaterialHandle generateDefaultMaterial(ResourceManager& resourceManager);

} // namespace Engine
