#pragma once

#include <memory>
#include <vector>

#include "system/script/behavior.h"

namespace Vkm::Engine {

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
};

} // namespace Vkm::Engine
