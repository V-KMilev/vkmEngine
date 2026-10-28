#pragma once

#include <string>

namespace Vkm::Engine {

/**
 * @brief An authored reference to a prefab file, held as its project-relative path.
 *
 * A behavior field such as `PrefabRef impact;`: the inspector offers the project's prefabs, and a
 * scene saves the path as a plain string. An empty path means "none". Behavior::instantiate and
 * instantiateShared take one.
 */
struct PrefabRef {
    std::string path;  ///< Project-relative, as "prefabs/impact.json"; empty for none.

    /// Whether it names no prefab.
    bool empty() const { return path.empty(); }
};

} // namespace Vkm::Engine
