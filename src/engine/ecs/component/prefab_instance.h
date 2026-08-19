#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Engine {

/**
 * @brief One field of one component of one entity, differing from the prefab.
 *
 * The address is (uid, component, field): which entity inside the subtree, which
 * component on it, which field of that component. @ref PrefabEntity explains why
 * the entity half is a number rather than a name or a position.
 *
 * The value is the field's own serialized JSON, as text - "14.0",
 * "[1.0,0.62,0.24]", "\"iron_rusted\"". Keeping it as text rather than a parsed
 * object is what lets this live in ecs/component/ without dragging the JSON
 * library in behind it, and dump/parse round-trips a float exactly. It also
 * means an override the current prefab no longer has a home for survives in
 * memory and is written back out unchanged, instead of being quietly lost the
 * first time the scene is saved.
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
 * Its presence is what lets a scene store the instance as a reference: the
 * saver writes this entity's source path, its Transform and its overrides, skips
 * the whole subtree beneath it, and the loader rebuilds that subtree from the
 * prefab. So the prefab file stays the single definition of what the thing is,
 * and editing it changes every instance.
 *
 * Only on the root. The entities the prefab creates below it carry
 * @ref PrefabEntity instead - they are rebuilt from the file each load, and
 * marking them as instances would invite treating them as independently
 * editable, which they are not. What varies per instance is the root's pose and
 * this list of overrides, and nothing else.
 */
struct PrefabInstance {
    std::string                 source;     ///< Prefab file path, e.g. "prefabs/lamp_post.json".
    std::vector<PrefabOverride> overrides;  ///< Per-instance deltas; written by SceneSerializer.
};

} // namespace Engine
