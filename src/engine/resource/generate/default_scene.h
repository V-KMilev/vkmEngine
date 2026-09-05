#pragma once

#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;

/**
 * @brief Seed @p scene with the minimum a scene needs to be looked at.
 *
 * An eye, a key light, and something for the light to fall on. Shared so that
 * starting the engine with no scene and choosing New Scene in the editor
 * produce the same thing - they are the same question asked twice, and
 * answering it in two places is how the two drift apart.
 *
 * Adds only entities and components: no undo entries, no selection, no toast,
 * so a caller can use it for a brand-new scene without side effects.
 *
 * @param scene     Scene to seed; expected to be empty.
 * @param resources Owns the cube's mesh and material.
 * @return The camera entity, for whoever drives the view.
 */
EntityId buildDefaultScene(Scene& scene, ResourceManager& resources);

} // namespace Vkm::Engine
