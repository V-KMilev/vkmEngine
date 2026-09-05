#pragma once

#include <memory>
#include <type_traits>

#include "ecs/entity.h"

#include "core/system.h"

#include "framework/command.h"
#include "framework/editor_context.h"
#include "framework/editor_commands.h"
#include "framework/editor_state.h"
#include "framework/prefab_overrides.h"

namespace Vkm::Engine {

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

/**
 * @brief A component held open for editing, which cannot be edited without an undo step.
 *
 * `design.md` states as an absolute that every editor mutation goes through an
 * undoable command, and a rule written as four lines a call site has to remember
 * in order is a rule the call sites enforce - which means it is not enforced.
 * This is the enforcement: a mutable component reference is not otherwise
 * obtainable in `src/editor`.
 *
 *     EditScope<Mesh> mesh(ec, id, "Assign Material");
 *     mesh->material = MaterialHandle{key};
 *
 * The step is pushed when the scope closes, through @ref editStep, so an
 * instance override is still recorded as an override and a Transform still
 * coalesces without the call site knowing.
 *
 * It pushes by default and @ref discard opts out, deliberately: forgetting to
 * push loses an undo step silently, while a redundant step is one visible line
 * in the history.
 *
 * Not for a drag. A gesture that runs over many frames marks the scene dirty as
 * it goes and pushes one step at the end, which is @ref editStep directly.
 *
 * @tparam T Component type, which must already be on the entity.
 */
template <typename T>
class EditScope {
    public:
        /**
         * @brief Open @p id's T for editing, to be pushed as @p label.
         *
         * @param scene     Scene holding the entity.
         * @param resources Resolves asset handles to names.
         * @param state     Editor state holding the history and the dirty flag.
         * @param id        Entity being edited.
         * @param label     History entry text; must outlive the scope.
         */
        EditScope(Scene& scene, ResourceManager& resources, EditorState& state,
                  EntityId id, const char* label)
            : m_scene(scene)
            , m_resources(resources)
            , m_state(state)
            , m_id(id)
            , m_label(label)
            , m_live(scene.get<T>(id))
            , m_before(m_live) {}

        /**
         * @brief The same, for the panels and overlays that already hold a context.
         *
         * @param ec    Editor context; its frame carries the scene and resources.
         * @param id    Entity being edited.
         * @param label History entry text; must outlive the scope.
         */
        EditScope(EditorContext& ec, EntityId id, const char* label)
            : EditScope(ec.frame.scene, ec.frame.resources, ec.state, id, label) {}

        ~EditScope() {
            if (m_discarded) return;
            pushEdit<T>(m_scene, m_resources, m_state, m_id, m_before, m_live, m_label);
        }

        EditScope(const EditScope& other) = delete;
        EditScope& operator=(const EditScope& other) = delete;

        EditScope(EditScope && other) = delete;
        EditScope& operator=(EditScope && other) = delete;

    public:
        /// The component being edited.
        T* operator->() const noexcept { return &m_live; }

        /// The component being edited.
        T& operator*() const noexcept { return m_live; }

        /**
         * @brief Close without a step: the edit did not happen after all.
         *
         * For a path that opens the scope before it knows whether it will write
         * - a dialog the author cancels, a value that came back unchanged. The
         * component is left as it stands, so a scope that already wrote and then
         * discards keeps the write and loses only the undo, which is why this is
         * for "nothing happened" rather than for "undo what I just did".
         */
        void discard() noexcept { m_discarded = true; }

    private:
        Scene&           m_scene;
        ResourceManager& m_resources;
        EditorState&     m_state;
        EntityId         m_id;
        const char*      m_label;
        T&               m_live;
        T                m_before;
        bool             m_discarded = false;
};

} // namespace Vkm::Engine
