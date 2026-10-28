#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/reflect.h"
#include "ecs/entity.h"
#include "ecs/scene.h"
#include "system/script/behavior.h"
#include "system/script/behavior_registry.h"

namespace Vkm::Engine {

/**
 * @brief A behavior whose type the registry does not know, kept verbatim.
 *
 * Usually the module is missing or failed to build. Written back unread (see
 * ComponentSerializer), so a save meanwhile loses nothing.
 */
struct UnknownBehavior {
    std::string type;
    std::string properties;  ///< Its property object, as JSON text, untouched.

    /// How many constructed behaviors are listed ahead of it: list order is run order, so its place among
    /// them must survive a save. removeBehavior keeps it in step.
    size_t constructedBefore = 0;
};

/**
 * @brief ECS component attaching owned, polymorphic Behaviors to an entity.
 *
 * Move-only, the documented exception to the plain-aggregate rule (code-style.md
 * section 10.1); duplication deep-copies through Behavior::clone().
 */
struct ScriptComponent {
    std::vector<std::unique_ptr<Behavior>> behaviors;

    /// Behaviors held as text because no type of that name is registered.
    std::vector<UnknownBehavior> unknown;
};

/**
 * @brief Remove @p sc's constructed behavior at @p index, each held one keeping its place among the rest.
 *
 * @param sc    The component.
 * @param index Its position in ScriptComponent::behaviors; past the end removes nothing.
 */
inline void removeBehavior(ScriptComponent& sc, size_t index) {
    if (index >= sc.behaviors.size()) return;
    sc.behaviors.erase(sc.behaviors.begin() + static_cast<std::ptrdiff_t>(index));
    for (UnknownBehavior& held : sc.unknown) {
        if (held.constructedBefore > index) --held.constructedBefore;
    }
}

/**
 * @brief Report a behavior added in code whose type the registry does not know.
 *
 * @param typeName The unregistered type's name.
 */
void reportUnregisteredBehavior(const char* typeName);

/**
 * @brief Attach a new T to @p entity, giving it a ScriptComponent if it has none.
 *
 * Added during play, it starts on the next simulation tick. An unregistered type
 * still runs but is reported, since a saved scene could not load it back.
 *
 * @code
 * Spinner& spinner = addBehavior<Spinner>(scene, cube);
 * spinner.degreesPerSecond = 180.0f;
 * @endcode
 *
 * @tparam T Behavior subclass with a VKM_REFLECT block, default-constructible.
 * @param scene  Scene holding the entity.
 * @param entity Entity to attach it to; must be alive.
 * @return The new behavior, owned by the entity's ScriptComponent.
 */
template<typename T>
T& addBehavior(Scene& scene, EntityId entity) {
    const char* name = Reflect::Traits<T>::NAME;
    if (!BehaviorRegistry::get().contains(name)) reportUnregisteredBehavior(name);

    auto instance = std::make_unique<T>();
    T& behavior = *instance;
    ScriptComponent* script = scene.tryGet<ScriptComponent>(entity);
    if (!script) script = &scene.add(entity, ScriptComponent{});
    script->behaviors.push_back(std::move(instance));
    return behavior;
}

} // namespace Vkm::Engine
