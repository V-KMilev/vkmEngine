#pragma once

#include <memory>
#include <type_traits>

#include "ecs/entity.h"

#include "framework/command.h"
#include "framework/editor_commands.h"
#include "framework/editor_state.h"
#include "framework/prefab_overrides.h"

namespace Engine {

class Scene;
class ResourceManager;

/**
 * @brief The undo step a component edit on @p id deserves.
 *
 * An override inside a prefab instance, and the plain edit for the type
 * everywhere else - a Transform coalesces through TransformChangeCommand,
 * anything else through ComponentEditCommand<T>. Which of the three applies is a
 * property of the entity and the type rather than a decision the call site
 * makes, so it is decided here once and every inspector, panel and gizmo asks
 * the same question the same way.
 *
 * The instance *root's* Transform takes the plain edit too: the scene stores
 * that pose itself, and PrefabOverrides::record says so by declining it.
 *
 * @tparam T Component type.
 * @param scene     Scene holding the entity.
 * @param resources Resolves asset handles to names.
 * @param id        Entity that was edited.
 * @param before    The component as it was before the edit.
 * @param after     The component as it is now.
 * @param label     History entry text.
 * @return The undo step; never null.
 */
template <typename T>
std::unique_ptr<Command> editStep(Scene& scene, ResourceManager& resources, EntityId id,
                                  const T& before, const T& after, const char* label) {
    if (auto step = PrefabOverrides::record<T>(scene, resources, id, before, after, label)) {
        return step;
    }
    if constexpr (std::is_same_v<T, Transform>) {
        return std::make_unique<TransformChangeCommand>(id, before, after, label);
    } else {
        return std::make_unique<ComponentEditCommand<T>>(id, before, after, label);
    }
}

/**
 * @brief Push @ref editStep onto the history and mark the scene unsaved.
 *
 * What a call site that finishes its edit in one gesture wants. A drag that
 * marks the scene dirty as it goes and pushes once at the end takes editStep
 * directly instead.
 *
 * @tparam T Component type.
 * @param scene     Scene holding the entity.
 * @param resources Resolves asset handles to names.
 * @param state     Editor state holding the history and the dirty flag.
 * @param id        Entity that was edited.
 * @param before    The component as it was before the edit.
 * @param after     The component as it is now.
 * @param label     History entry text.
 */
template <typename T>
void pushEdit(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id,
              const T& before, const T& after, const char* label) {
    state.commands.push(editStep<T>(scene, resources, id, before, after, label));
    state.markSceneDirty();
}

} // namespace Engine
