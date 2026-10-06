#pragma once

#include "resource/asset/material_asset.h"
#include "resource/resource_handle.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief The graph's default PBR material, created on first ask.
 *
 * Neutral white dielectric with the 1x1 built-in textures bound. One per graph,
 * keyed on "material:default"; copy it rather than renaming it, since the name
 * is what everything resolves by.
 *
 * @param resourceManager Where it and its textures are found or created.
 * @return Handle to the graph's default material.
 */
MaterialHandle generateDefaultMaterial(ResourceManager& resourceManager);

} // namespace Vkm::Engine
