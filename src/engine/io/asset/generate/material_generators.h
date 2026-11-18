#pragma once

#include "resource/asset/material_asset.h"
#include "resource/handle.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief The graph's default PBR material, created on first ask.
 *
 * MaterialAsset's defaults, with no maps: a white dielectric. One per graph,
 * keyed on "material:default"; copy it rather than renaming it, since the name
 * is what everything resolves by.
 *
 * @param resourceManager Where it is found or created.
 * @return Handle to the graph's default material.
 */
MaterialHandle generateDefaultMaterial(ResourceManager& resourceManager);

/**
 * @brief The graph's default water's look, created on first ask: smooth, a deep green-blue
 *        scattering its light. A mesh drawn in it is water with a Water component.
 *
 * Keyed on "material:water", as the default is.
 *
 * @param resourceManager Where it is found or created.
 * @return Handle to the graph's water material.
 */
MaterialHandle generateWaterMaterial(ResourceManager& resourceManager);

/**
 * @brief The graph's default sea's look, created on first ask: dark, near mirror-smooth,
 *        scattering a deep green-blue.
 *
 * Keyed on "material:sea".
 *
 * @param resourceManager Where it is found or created.
 * @return Handle to the graph's sea material.
 */
MaterialHandle generateSeaMaterial(ResourceManager& resourceManager);

} // namespace Vkm::Engine
