#pragma once

#include <string>

#include "core/reflect.h"

namespace Engine {

/**
 * @brief Marks an entity as the root of an instanced prefab.
 *
 * Its presence is what lets a scene store the instance as a reference: the
 * saver writes this entity's source path and its Transform and skips the whole
 * subtree beneath it, and the loader rebuilds that subtree from the prefab. So
 * the prefab file stays the single definition of what the thing is, and editing
 * it changes every instance.
 *
 * Only on the root. The entities the prefab creates below it carry nothing -
 * they are rebuilt from the file each load, and marking them would invite
 * treating them as independently editable, which they are not.
 */
struct PrefabInstance {
    std::string source;  ///< Prefab file path, e.g. "prefabs/lamp_post.json".
};
} // namespace Engine

VKM_REFLECT_BEGIN(::Engine::PrefabInstance)
    VKM_F(source)
VKM_REFLECT_END()
