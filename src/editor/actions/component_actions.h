#pragma once

#include <memory>
#include <string>
#include <utility>

#include "ecs/entity.h"

#include "undo/component_commands.h"
#include "undo/prefab_overrides.h"
#include "editor_state.h"

namespace Vkm::Engine {

class Scene;

namespace EditorActions {

/**
 * @brief Add @p component to @p id as one undo step.
 *
 * Inside a prefab instance it warns that a component the prefab lacks is not stored.
 *
 * @tparam T A value row of VKM_SCENE_COMPONENTS, or the command does not exist.
 * @param scene Holds the entity; it must not already carry a T.
 * @param state Takes the step and the warning.
 * @param id Entity to add to.
 * @param component The value added.
 * @param title The component's name as the author sees it, for the warning.
 * @param label History entry text; the command keeps the pointer.
 * @return The component as stored.
 */
template<typename T>
T& addComponent(
    Scene& scene,
    EditorState& state,
    EntityId id,
    T component,
    const char* title,
    const char* label
) {
    T& stored = scene.add(id, std::move(component));
    state.pushStep(std::make_unique<AddComponentCommand<T>>(id, stored, label));
    PrefabOverrides::warnComponentIsPrefabs(scene, state, id, title, "is not stored in the scene");
    return stored;
}

/**
 * @brief Add a behavior of @p type to @p id, and a ScriptComponent if it has none, as one undo step.
 *
 * @param scene Holds the entity.
 * @param state Takes the step and the warning.
 * @param id    Entity to add to.
 * @param type  A registered behavior type.
 */
void addBehavior(Scene& scene, EditorState& state, EntityId id, const std::string& type);

} // namespace EditorActions

} // namespace Vkm::Engine
