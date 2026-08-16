#pragma once

#include "resource/asset/material_asset.h"
#include "resource/resource_handle.h"

namespace Engine {

class ResourceManager;

/**
 * @brief Generate a default PBR material.
 *
 * Neutral white dielectric. Also generates the 1x1 built-in textures (white,
 * black, normal, gray) and assigns them, so every slot is bound.
 *
 * @param resourceManager Resource manager to create material in.
 * @return Handle to the generated material.
 */
MaterialHandle generateDefaultMaterial(ResourceManager& resourceManager);

} // namespace Engine
