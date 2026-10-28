#pragma once

#include "ecs/entity.h"
#include "ecs/scene.h"
#include "io/scene/prefab.h"

namespace Vkm::Engine {

/**
 * @brief Whether @p parent lies inside a prefab instance, its root included, and so takes no new child.
 *
 * An instance's interior is built from its prefab and the scene does not store it, so an entity
 * made, moved or copied there would be gone at the next load. Free of ImGui so it is tested
 * headlessly.
 *
 * @param scene  Holds @p parent.
 * @param parent The parent asked for; null or dead refuses nothing.
 * @return true when an entity must not be put under @p parent.
 */
inline bool refusesNewChild(const Scene& scene, EntityId parent) {
    return parent && scene.isAlive(parent) && Prefab::instanceRootOf(scene, parent);
}

/**
 * @brief The parent a copy asked to go under @p wanted lands under: @p wanted while it lives and
 *        does not refuse it (refusesNewChild), else none, the copy a root where it stood in the world.
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
