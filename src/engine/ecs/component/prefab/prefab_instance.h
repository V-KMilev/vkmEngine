#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Vkm::Engine {

/**
 * @brief One field of one component of one entity, differing from the prefab.
 *
 * Addressed by (uid, component, field); see @ref PrefabEntity. The value is JSON text,
 * which keeps the JSON library out of ecs/component/, round-trips floats exactly, and
 * lets an override with no current home be written back unchanged.
 */
struct PrefabOverride {
    uint32_t    uid = 0;    ///< Entity inside the prefab; 0 is the root.
    std::string component;  ///< Component key, e.g. "Light".
    std::string field;      ///< Field key within that component, e.g. "intensity".
    std::string value;      ///< The field's value as JSON text.
};

/**
 * @brief Marks an entity as the root of an instanced prefab.
 *
 * A scene saves the source, Transform and overrides and skips the subtree, which the
 * loader rebuilds from the prefab. Only on the root; entities below carry @ref PrefabEntity.
 */
struct PrefabInstance {
    std::string                 source;     ///< Prefab file path, e.g. "prefabs/lamp_post.json".
    std::vector<PrefabOverride> overrides;  ///< Per-instance deltas.
};

} // namespace Vkm::Engine
