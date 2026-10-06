#pragma once

#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;

/**
 * @brief Seed @p scene with the minimum a scene needs to be looked at.
 *
 * A camera, a key light and a cube. No undo entries, selection or toast.
 *
 * @param scene     Scene to seed; expected to be empty.
 * @param resources Owns the cube's mesh and material.
 * @return The camera entity, for whoever drives the view.
 */
EntityId buildDefaultScene(Scene& scene, ResourceManager& resources);

} // namespace Vkm::Engine
