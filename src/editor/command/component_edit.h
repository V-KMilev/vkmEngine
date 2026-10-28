#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>
#include <type_traits>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include "core/reflect.h"
#include "core/system.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/entity.h"
#include "ecs/scene.h"
#include "command/command.h"
#include "command/editor_commands.h"
#include "command/command_host.h"
#include "command/command_stack.h"
#include "command/prefab_overrides.h"
#include "system/physics/authoring/mesh_collider.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief @p into with @p from's authored fields: what the scene file saves.
 *
 * Session state - a voice started, a body asleep - stays @p into's, so a value copied
 * in a session and pasted after Stop brings the tuning and nothing of the run. Only a
 * reflected T names its authored fields.
 *
 * @param into The component as it is.
 * @param from The component whose authored values are taken.
 * @return The mix.
 */
template <typename T>
T withAuthored(T into, const T& from) {
    static_assert(Reflect::IS_REFLECTED<T>, "only a reflected component names its authored fields");
    constexpr auto fields = Reflect::Traits<T>::fields();
    std::apply([&](const auto&... field) { ((into.*(field.ptr) = from.*(field.ptr)), ...); }, fields);
    return into;
}

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
 * @brief What an edit of a component leaves to work out, done on the live one.
 *
 * Called before the edit's step is cut, so the step holds what it settles. Nothing, but for
 * the overloads below.
 *
 * @param component The component as edited.
 * @param resources Resolves what it names.
 */
template <typename T>
void settleEdit(T& component, const ResourceManager& resources) {
    (void)component;
    (void)resources;
}

/// A body woken, so it runs as edited rather than sleeping through a change nothing pushed it for.
inline void settleEdit(Rigidbody& body, const ResourceManager&) {
    Rigidbody::wake(body);
}

/// A mesh part's triangles built for the mesh it now names.
inline void settleEdit(Collider& collider, const ResourceManager& resources) {
    syncMeshCollider(collider, resources);
}

/**
 * @brief The step for a Transform a clip's playhead posed, or null when it is none.
 *
 * The world is not stepping, so an editor shows the playhead by posing. Outside a session that is
 * the saved Transform, so it is recorded; in one it is session state, and a step would drop the
 * history Stop keeps.
 *
 * @param scene     Scene holding the entity.
 * @param resources Resolves asset handles.
 * @param id        The posed entity.
 * @param before    Its Transform before the frame's pose.
 * @param now       Its Transform now.
 * @param playing   Whether a session holds the scene.
 * @param label     History entry text; kept by pointer.
 * @return The step, or null when nothing moved or a session holds the scene.
 */
inline std::unique_ptr<Command> poseStep(
    Scene& scene,
    ResourceManager& resources,
    EntityId id,
    const Transform& before,
    const Transform& now,
    bool playing,
    const char* label
) {
    const bool posed = now.position != before.position || now.rotation != before.rotation
        || now.scale != before.scale;
    if (!posed || playing) return nullptr;
    return editStep<Transform>(scene, resources, id, before, now, label);
}

/// Whether a field type is a list, which changedFields never shares.
template <typename T>
inline constexpr bool IS_LIST = false;
template <typename T, typename A>
inline constexpr bool IS_LIST<std::vector<T, A>> = true;

/**
 * @brief Which reflected fields of @p after differ from @p before in what the scene file writes.
 *
 * Each field is tried alone on a copy of @p before and the two written forms compared,
 * so a field type needs no operator==. A field naming an entity is never counted: its
 * value means the same only on the entity it was set on. Nor is a list (a collider's
 * parts, LOD levels): it is shared whole or not at all, and one item edited must not
 * replace another entity's whole list.
 *
 * @param before    The component before an edit.
 * @param after     The component after it.
 * @param resources Resolves asset handles, as a save does.
 * @return Bit i for reflected field i, in Traits order.
 */
template <typename T>
uint64_t changedFields(const T& before, const T& after, const ResourceManager& resources) {
    uint64_t changed = 0;
    if constexpr (Reflect::IS_REFLECTED<T>) {
        constexpr auto fields = Reflect::Traits<T>::fields();
        static_assert(std::tuple_size_v<decltype(fields)> <= 64, "a field mask is 64 bits");
        const nlohmann::json was = PrefabOverrides::serialize(before, resources);
        size_t index = 0;
        const auto probe = [&](const auto& field) {
            using Value = std::decay_t<decltype(before.*(field.ptr))>;
            if constexpr (!std::is_same_v<Value, EntityId> && !IS_LIST<Value>) {
                T one = before;
                one.*(field.ptr) = after.*(field.ptr);
                if (PrefabOverrides::serialize(one, resources) != was) changed |= uint64_t{1} << index;
            }
            ++index;
        };
        std::apply([&](const auto&... field) { (probe(field), ...); }, fields);
    }
    return changed;
}

/**
 * @brief @p into with the fields of @p from that @p mask names; the rest stay its own.
 *
 * @param into The component taking them.
 * @param from The component giving them.
 * @param mask changedFields's answer.
 * @return The mix.
 */
template <typename T>
T withFields(T into, const T& from, uint64_t mask) {
    if constexpr (Reflect::IS_REFLECTED<T>) {
        size_t index = 0;
        const auto take = [&](const auto& field) {
            if (mask & (uint64_t{1} << index)) into.*(field.ptr) = from.*(field.ptr);
            ++index;
        };
        std::apply([&](const auto&... field) { (take(field), ...); }, Reflect::Traits<T>::fields());
    }
    return into;
}

/**
 * @brief The undo step for an edit of @p id's T, carried to every other selected entity holding a T.
 *
 * Each takes the fields the edit changed (changedFields) and keeps the rest of its own.
 * One CompositeCommand, so a drag across several entities still merges into one step.
 *
 * @param scene     Scene holding the entities.
 * @param resources Resolves asset handles.
 * @param selection The selected entities; @p id is one of them.
 * @param id        The entity edited.
 * @param before    Its T before the edit.
 * @param after     Its T now, already applied.
 * @param label     History entry text; kept by pointer.
 * @return The step: editStep's alone when no other selected entity holds a T.
 */
template <typename T>
std::unique_ptr<Command> editSelectionStep(
    Scene& scene,
    ResourceManager& resources,
    const std::vector<EntityId>& selection,
    EntityId id,
    const T& before,
    const T& after,
    const char* label
) {
    std::vector<EntityId> others;
    for (const EntityId other : selection) {
        if (other != id && scene.isAlive(other) && scene.has<T>(other)) others.push_back(other);
    }
    if (others.empty()) return editStep<T>(scene, resources, id, before, after, label);

    auto step = std::make_unique<CompositeCommand>(label);
    step->add(editStep<T>(scene, resources, id, before, after, label));
    const uint64_t mask = changedFields(before, after, resources);
    if (mask == 0) return step;
    for (const EntityId other : others) {
        T&      value = scene.get<T>(other);
        const T was   = value;
        value = withFields(value, after, mask);
        settleEdit(value, resources);
        step->add(editStep<T>(scene, resources, other, was, value, label));
    }
    return step;
}

/**
 * @brief @p other with what a Transform edit moved: position and scale per axis, and the
 *        turn the edited entity took.
 *
 * A move of one axis across a selection sets that axis alone, so nothing is stacked; a
 * turn turns each by the same amount, never re-reading its angles, which would flip one
 * past a right angle.
 *
 * @param other  The Transform taking the edit.
 * @param before The edited entity's before the edit.
 * @param after  The edited entity's now.
 * @return @p other, the moved axes set and the turn applied.
 */
inline Transform withMovedAxes(Transform other, const Transform& before, const Transform& after) {
    for (int axis = 0; axis < 3; ++axis) {
        if (after.position[axis] != before.position[axis]) other.position[axis] = after.position[axis];
        if (after.scale[axis] != before.scale[axis])       other.scale[axis]    = after.scale[axis];
    }
    if (after.rotation != before.rotation) {
        other.rotation = glm::normalize(after.rotation * glm::inverse(before.rotation) * other.rotation);
    }
    return other;
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
