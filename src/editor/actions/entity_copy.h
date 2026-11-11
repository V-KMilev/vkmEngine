#pragma once

#include <memory>

#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/component/scene/transform.h"
#include "ecs/entity.h"

#include "undo/command.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;
struct EditorState;
struct SubtreeSnapshot;

namespace EditorActions {

/**
 * @brief A copy and the step that removes it, together because the batch path puts the step in its composite.
 */
struct Duplicate {
    EntityId                 entity;
    std::unique_ptr<Command> step;
};

/**
 * @brief Where @p id stands in the world: the pose it keeps when copied out from under its parent.
 *
 * @param scene Holds the entity.
 * @param id    The entity copied.
 * @return Its world pose; the identity for an entity with no Transform.
 */
Transform worldPoseOf(const Scene& scene, EntityId id);

/**
 * @brief Make @p snap what duplicateEntity says a copy of a subtree is: nothing running, and no
 *        prefab's but that of an instance copied with it.
 *
 * @param snap The subtree being copied, root first.
 */
void readyCopy(SubtreeSnapshot& snap);

/**
 * @brief Build an instance of @p instance's prefab at @p at, under @p parent.
 *
 * @param scene     Receives the instance.
 * @param resources Resolves the prefab's asset names to handles.
 * @param state     Takes the toast when the prefab cannot be built.
 * @param instance  The instance copied: its prefab and overrides.
 * @param at        Its pose, local to @p parent.
 * @param parent    Puts it under this entity, or null for the root.
 * @param label     History entry text.
 * @return The copy and its step; empty when the prefab could not be built.
 */
Duplicate placeInstance(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    const PrefabInstance& instance,
    const Transform& at,
    EntityId parent,
    const char* label
);

} // namespace EditorActions

} // namespace Vkm::Engine
