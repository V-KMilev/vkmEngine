#pragma once

#include "resource/asset/material_asset.h"
#include "resource/resource_handle.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief The graph's default PBR material, created on first ask.
 *
 * Neutral white dielectric. Also generates the 1x1 built-in textures (white,
 * black, normal, gray) and assigns them, so every slot is bound.
 *
 * One asset per graph, keyed on the name "material:default", exactly as the
 * built-in textures it binds are shared: a second material under that name
 * would be given a " (N)" suffix, and the name is the serializable identity, so
 * the suffix becomes what the scene file and the cooked library call it. A
 * caller wanting a material of its own copies this one rather than renaming it.
 *
 * @param resourceManager Resource manager to look the material up in, and to
 *        create it and its textures in when it is not there yet.
 * @return Handle to the graph's default material.
 */
MaterialHandle generateDefaultMaterial(ResourceManager& resourceManager);

/**
 * @brief Build a new default PBR material, without looking for an existing one.
 *
 * The same neutral white dielectric generateDefaultMaterial hands out, as an
 * asset of its own. For the caller that is going to name the result itself: a
 * loader rebuilding a material whose recipe says "default" renames what it gets
 * to the name the document recorded, and renaming the graph's shared default
 * would take that name away from everything resolving it.
 *
 * @param resourceManager Resource manager receiving the material and the 1x1
 *        built-in textures it binds.
 * @return Handle to the newly added material, named "material:default" until
 *         the caller renames it.
 */
MaterialHandle buildDefaultMaterial(ResourceManager& resourceManager);

} // namespace Vkm::Engine
