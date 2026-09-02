#pragma once

#include <memory>
#include <string>
#include <vector>

#include "system/script/behavior.h"

namespace Vkm::Engine {

/**
 * @brief A behavior whose type the registry does not know, kept verbatim.
 *
 * The registry only knows the types the loaded gameplay module registered, so
 * an unknown type almost always means the module is not there - it failed to
 * build, the reload failed, the project was opened without one.
 *
 * The loader keeps the document instead of dropping it and the saver writes it
 * back out unread, so a save while the module is broken loses nothing and the
 * behavior comes back whole the moment its type is registered again.
 */
struct UnknownBehavior {
    std::string type;        ///< The type name that was asked for.
    std::string properties;  ///< Its property object, as JSON text, untouched.

    /**
     * @brief Where it sat in the list it was read from.
     *
     * Kept because the order of this list is the order the behaviors run in,
     * so appending the held ones at the end would rewrite that on the first
     * save - quietly, and only for the scenes that were open while a module
     * was missing.
     */
    size_t index = 0;
};

/**
 * @brief ECS component attaching owned, polymorphic Behaviors to an entity.
 *
 * Move-only (it owns unique_ptrs) - the documented exception to the plain-
 * aggregate component rule (style guide section 13). SparseSet stores it fine
 * via its move path (swap-and-pop uses std::move), and SceneSerializer's
 * loader moves the staged value into the scene.
 *
 * Per-behavior deep copy for entity duplication goes through Behavior::clone().
 */
struct ScriptComponent {
    std::vector<std::unique_ptr<Behavior>> behaviors;

    /**
     * @brief Behaviors held as text because no type of that name is registered.
     *
     * Written back at the position each was read from, not appended - the order
     * of a behavior list is the order they run in, and each entry carries the
     * index it needs for that. So a scene that round-trips with the module
     * missing keeps every behavior and the order they ran in.
     */
    std::vector<UnknownBehavior> unknown;
};

} // namespace Vkm::Engine
