#pragma once

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

    /// Position in the list read from; list order is run order, so it must survive a save.
    size_t index = 0;
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
 * @brief The entity carrying a behavior named @p typeName with the lowest slot.
 *
 * @param scene    Scene to search.
 * @param typeName Registered behavior name.
 * @return The entity, or a null EntityId when nothing carries one.
 */
EntityId findEntityWithBehaviorNamed(Scene& scene, std::string_view typeName);

/**
 * @brief Report a behavior added in code whose type the registry does not know.
 *
 * @param typeName The unregistered type's name.
 */
void reportUnregisteredBehavior(const char* typeName);

/**
 * @brief The entity carrying a T, for the behaviors a game has one of.
 *
 * The lowest slot answers, so the choice is the same on every machine.
 *
 * @tparam T Behavior subclass with a VKM_REFLECT block.
 * @param scene Scene to search.
 * @return The entity, or a null EntityId when nothing carries one.
 */
template<typename T>
EntityId findEntityWithBehavior(Scene& scene) {
    return findEntityWithBehaviorNamed(scene, Reflect::Traits<T>::NAME);
}

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
