#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Vkm::Engine {

class BehaviorBase;

/**
 * @brief A behavior whose type the registry does not know, kept verbatim.
 *
 * Usually the module is missing or failed to build. Written back unread (see
 * ComponentSerializer), so a save meanwhile loses nothing.
 */
struct UnknownBehavior {
    std::string type;
    std::string properties;  ///< Its property object, as JSON text, untouched.

    size_t constructedBefore = 0;  ///< How many constructed behaviors are listed ahead of it: list order is
                                   ///< run order, so its place among them must survive a save. removeBehavior
                                   ///< keeps it in step.
};

/**
 * @brief ECS component attaching owned, polymorphic Behaviors to an entity.
 *
 * Move-only, the documented exception to the plain-aggregate rule (architecture.md,
 * Known exceptions); duplication deep-copies through BehaviorBase::clone(). Saved under "Script"
 * as one field holding the whole list, which a prefab override cannot address inside, so an
 * instance records none and a hand-written "Script" override is reported and ignored.
 */
struct ScriptComponent {
    ScriptComponent();
    ~ScriptComponent();

    ScriptComponent(const ScriptComponent& other) = delete;
    ScriptComponent& operator=(const ScriptComponent& other) = delete;

    ScriptComponent(ScriptComponent && other) noexcept;
    ScriptComponent& operator=(ScriptComponent && other) noexcept;

    std::vector<std::unique_ptr<BehaviorBase>> behaviors;

    std::vector<UnknownBehavior> unknown;  ///< Behaviors held as text because no type of that name is
                                           ///< registered.
};

} // namespace Vkm::Engine
