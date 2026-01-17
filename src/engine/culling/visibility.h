#pragma once

#include <vector>
#include "entity.h"

namespace Engine {
    class Scene;
    class ResourceManager;
}

namespace Engine {

/**
 * @brief Holds the set of visible entity IDs after visibility determination.
 */
struct Visibility {
    std::vector<EntityId> entities;    ///< List of visible entity IDs
};

/**
 * @brief Builds a Visibility structure containing visible entities based on the active camera.
 * 
 * Performs frustum culling of mesh entities in the scene using the current camera.
 * 
 * @param scene The scene to process entities from.
 * @param resources Resource manager needed to fetch mesh data for culling.
 * @return Visibility The visibility structure listing visible entity IDs.
 */
Visibility buildVisibility(
    const Scene& scene,
    const ResourceManager& resources
);

} // namespace Engine
