#pragma once

#include <algorithm>
#include <vector>

#include "ecs/entity.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/scene.h"
#include "ecs/prefab_instances.h"

namespace Vkm::Engine {

/**
 * @brief What a viewport click on @p hit selects: one level further in than the selection.
 *
 * A part of a prefab instance selects the instance first, as a placed thing is moved whole; once
 * the selection is inside it, the next click selects the part. Free of ImGui so it is tested
 * headlessly.
 *
 * @param scene  Holds the entities.
 * @param hit    What the click landed on.
 * @param active The active selection before the click; may be null.
 * @return @p hit, or the root of the prefab instance it is part of.
 */
inline EntityId clickTarget(const Scene& scene, EntityId hit, EntityId active) {
    const EntityId root = Prefab::instanceRootOf(scene, hit);
    if (!root) return hit;
    const bool inside = active == root || HierarchyOperations::isAncestorOf(scene, root, active);
    return active && inside ? hit : root;
}

/**
 * @brief Whether a viewport click may land on @p entity: neither it nor an ancestor is held.
 *
 * @param scene Holds the entities.
 * @param entity What a click would land on.
 * @param held The Hierarchy's hidden or locked entities, each standing for its subtree.
 * @return False under one of @p held.
 */
inline bool pickable(const Scene& scene, EntityId entity, const std::vector<EntityId>& held) {
    if (held.empty()) return true;
    bool free = true;
    HierarchyOperations::forSelfAndAncestors(scene, entity, [&](EntityId at) {
        free = std::find(held.begin(), held.end(), at) == held.end();
        return free;
    });
    return free;
}

} // namespace Vkm::Engine
