#pragma once

#include <memory>

#include "ecs/entity.h"

#include "core/system.h"

#include "command/command.h"
#include "command/editor_commands.h"
#include "command/command_host.h"
#include "command/command_stack.h"
#include "command/prefab_overrides.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;

/**
 * @brief The undo step a component edit on @p id deserves.
 *
 * An override inside a prefab instance, and ComponentEditCommand<T>, which
 * coalesces, everywhere else.
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
std::unique_ptr<Command> editStep(
    Scene& scene,
    ResourceManager& resources,
    EntityId id,
    const T& before,
    const T& after,
    const char* label
) {
    if (auto step = PrefabOverrides::record<T>(scene, resources, id, before, after, label)) {
        return step;
    }
    return std::make_unique<ComponentEditCommand<T>>(id, before, after, label);
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
 * @param host      Where the step is pushed and the dirty flag set.
 * @param id        Entity that was edited.
 * @param before    The component as it was before the edit.
 * @param after     The component as it is now.
 * @param label     History entry text.
 */
template <typename T>
void pushEdit(
    Scene& scene,
    ResourceManager& resources,
    CommandHost& host,
    EntityId id,
    const T& before,
    const T& after,
    const char* label
) {
    host.pushStep(editStep<T>(scene, resources, id, before, after, label));
}

/**
 * @brief A component held open for editing, which cannot be edited without an undo step.
 *
 *     EditScope<Mesh> mesh(scene, resources, state, id, "Assign Material");
 *     mesh->material = MaterialHandle{key};
 *
 * The step is pushed when the scope closes, through @ref editStep, so an
 * instance override is still recorded as an override and a Transform still
 * coalesces without the call site knowing.
 *
 * It pushes by default and @ref discard opts out: forgetting to push loses an
 * undo step silently, while a redundant step is one visible line in the history.
 *
 * Not for a drag. A gesture that runs over many frames marks the scene dirty as
 * it goes and pushes one step at the end, which is @ref editStep directly.
 *
 * The component is held by reference for the life of the scope, so nothing may
 * add or remove a T while one is open: that moves the storage the reference
 * points into.
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
         * @param host      Where the step is pushed and the dirty flag set.
         * @param id        Entity being edited.
         * @param label     History entry text; the pushed command keeps the
         *                  pointer, so it must outlive the command.
         */
        EditScope(Scene& scene, ResourceManager& resources, CommandHost& host, EntityId id, const char* label)
            : m_scene(scene)
            , m_resources(resources)
            , m_host(host)
            , m_id(id)
            , m_label(label)
            , m_live(scene.get<T>(id))
            , m_before(m_live) {}

        ~EditScope() {
            if (m_discarded) return;
            pushEdit<T>(m_scene, m_resources, m_host, m_id, m_before, m_live, m_label);
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
         * discards keeps the write and loses only the undo.
         */
        void discard() noexcept { m_discarded = true; }

    private:
        Scene&           m_scene;
        ResourceManager& m_resources;
        CommandHost&     m_host;
        EntityId         m_id;
        const char*      m_label;
        T&               m_live;
        T                m_before;
        bool             m_discarded = false;
};

} // namespace Vkm::Engine
