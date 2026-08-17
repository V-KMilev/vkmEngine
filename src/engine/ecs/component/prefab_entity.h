#pragma once

#include <cstdint>

namespace Engine {

/**
 * @brief The identity an entity carries inside an instanced prefab.
 *
 * An override has to name one entity in a prefab's subtree and keep naming it
 * after the prefab is re-saved with entities reordered, renamed, inserted or
 * removed. Nothing else in the engine survives that: entity ids are runtime
 * slots, array position moves, and Name is user-editable and not unique.
 *
 * So the prefab file carries a uid per entity, assigned once when the prefab is
 * written and never reused, and instancing stamps it onto the entity it built.
 * An override addresses that number.
 *
 * Runtime only - a scene never writes this, because a scene never writes a
 * prefab's subtree at all: it stores the instance as a reference and rebuilds
 * the entities from the file, which is where the uids come from.
 */
struct PrefabEntity {
    /// The prefab root's uid. Fixed, so an override on the root needs no lookup.
    static constexpr uint32_t ROOT = 0;

    uint32_t uid = ROOT;
};

} // namespace Engine
