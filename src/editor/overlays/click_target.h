#pragma once

#include <cstddef>
#include <vector>

#include "ecs/component/core/hierarchy.h"
#include "ecs/entity.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/scene.h"
#include "io/scene/prefab.h"

namespace Vkm::Engine {

/**
 * @brief What a viewport click on @p hit selects: one level further in than the selection.
 *
 * A part of a prefab instance selects the outermost instance first, as a placed thing
 * is moved whole; once the selection is inside an instance, the next click selects
 * the instance nested in it, then the part itself. Free of ImGui so it is tested headlessly.
 *
 * @param scene  Holds the entities.
 * @param hit    What the click landed on.
 * @param active The active selection before the click; may be null.
 * @return The entity to select.
 */
inline EntityId clickTarget(const Scene& scene, EntityId hit, EntityId active) {
    // The instance roots holding the hit, innermost first; bounded against a cyclic file.
    std::vector<EntityId> roots;
    size_t guard = scene.entityCount();
    for (EntityId at = hit; at && guard-- > 0;) {
        const EntityId root = Prefab::instanceRootOf(scene, at);
        if (!root) break;
        roots.push_back(root);
        const Hierarchy* node = scene.tryGet<Hierarchy>(root);
        at = node ? node->parent : EntityId{};
    }
    for (auto root = roots.rbegin(); root != roots.rend(); ++root) {
        const bool inside = active == *root || HierarchyOperations::isAncestorOf(scene, *root, active);
        if (!active || !inside) return *root;
    }
    return hit;
}

} // namespace Vkm::Engine
