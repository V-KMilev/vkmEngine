#pragma once

#include <cstdint>
#include <map>

#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;

namespace Prefab {

    /**
     * @brief The root of the prefab instance @p id belongs to: itself, or its
     *        nearest ancestor carrying PrefabInstance.
     *
     * Walks up, so descendants carry no bookkeeping that could fall out of sync.
     *
     * @param scene Scene holding the entity.
     * @param id    Entity to resolve.
     * @return The instance root, or a null id when @p id is not part of one.
     */
    EntityId instanceRootOf(const Scene& scene, EntityId id);

    /**
     * @brief Is @p id inside (but not the root of) a prefab instance?
     *
     * @param scene Scene holding the entity.
     * @param id    Entity to test.
     * @return True when an ancestor of @p id carries PrefabInstance.
     */
    bool isInsideInstance(const Scene& scene, EntityId id);

    /**
     * @brief Where one instance's built entities stand: prefab uid to slot.
     *
     * A scene keeps it beside each instance (`"slots"`), so a load builds every entity
     * back where it stood, and what names one by slot - a reference, a history -
     * still names it. A rebuild handed this puts each entity back.
     */
    using BuiltSlots = std::map<uint32_t, uint32_t>;

    /**
     * @brief Every instance's BuiltSlots, keyed by the slot its root holds.
     *
     * Comparable, so a caller that rebuilt a world can ask whether it came back
     * exactly.
     */
    using InstanceSlots = std::map<uint32_t, BuiltSlots>;

    /**
     * @brief Where the instance rooted at @p root put each of its entities.
     *
     * @param scene Scene holding the instance.
     * @param root  Instance root; itself not listed, since its slot is its own.
     * @return Each entity below @p root carrying a PrefabEntity, by uid.
     */
    BuiltSlots builtSlotsOf(const Scene& scene, EntityId root);

    /**
     * @brief builtSlotsOf for every instance in @p scene.
     *
     * @param scene Scene to read.
     * @return One entry per PrefabInstance, by the slot of its root.
     */
    InstanceSlots instanceSlotsOf(const Scene& scene);

} // namespace Prefab

} // namespace Vkm::Engine
