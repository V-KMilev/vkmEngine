#pragma once

#include "ecs/entity.h"
#include "ecs/scene.h"
#include "ecs/prefab_instances.h"

namespace Vkm::Engine {

/**
 * @brief Whether @p parent lies inside a prefab instance, its root included, and so takes no new child.
 *
 * The scene does not store an instance's interior, so an entity made, moved or copied there would be
 * gone at the next load.
 *
 * @param scene  Holds @p parent.
 * @param parent The parent asked for; null or dead refuses nothing.
 * @return true when an entity must not be put under @p parent.
 */
inline bool refusesNewChild(const Scene& scene, EntityId parent) {
    return parent && scene.isAlive(parent) && Prefab::instanceRootOf(scene, parent);
}

/**
 * @brief Where a copy asking for the parent @p wanted lands: under it while it lives and takes a child.
 *
 * Else at the root, where it stood in the world; see refusesNewChild.
 *
 * @param scene  Holds @p wanted.
 * @param wanted The parent the copy would take, such as its source's; may be null.
 * @return @p wanted, or null.
 */
inline EntityId copyParent(const Scene& scene, EntityId wanted) {
    if (!wanted || !scene.isAlive(wanted) || refusesNewChild(scene, wanted)) return {};
    return wanted;
}

} // namespace Vkm::Engine
