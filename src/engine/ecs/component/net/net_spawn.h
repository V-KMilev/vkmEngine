#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "ecs/component/core/transform.h"

namespace Vkm::Engine {

/**
 * @brief What a server built at this root after the scene loaded, so every client can build the same.
 *
 * Added by NetSession::spawn and replicated until confirmed, and to late joiners. A
 * client builds the prefab into the root and drops this. The subtree is not on the
 * wire but its slots are, so slots stay identical on every end. Runtime only; a
 * scene stores a built instance through PrefabInstance.
 */
struct NetSpawn {
    std::string prefab;  ///< Project-relative prefab path.
    Transform   at;      ///< Build pose, exact: a static body is never described again.

    /// Where the server built each entity below the root, by PrefabEntity uid
    /// (Prefab::BuiltSlots); empty for an instance too large to say it all.
    std::map<uint32_t, uint32_t> slots;
};

} // namespace Vkm::Engine
