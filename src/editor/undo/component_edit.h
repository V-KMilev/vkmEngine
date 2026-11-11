#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>
#include <type_traits>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "base/reflect/reflect.h"
#include "core/system.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/entity.h"
#include "ecs/scene.h"
#include "undo/command.h"
#include "undo/composite_command.h"
#include "undo/component_commands.h"
#include "undo/command_host.h"
#include "undo/command_stack.h"
#include "undo/prefab_overrides.h"
#include "ecs/component/physics/mesh_collider.h"
#include "system/physics/authoring/collider_fit.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief @p into with @p from's authored fields, the ones the scene file saves.
 *
 * Session state (a voice started, a body asleep) stays @p into's, so a paste after Stop brings tuning only.
 *
 * @param into The component as it is.
 * @param from The component whose authored values are taken.
 * @return @p into with every reflected field taken from @p from.
 */
template <typename T>
T withAuthored(T into, const T& from) {
    static_assert(Reflect::IS_REFLECTED<T>, "only a reflected component names its authored fields");
    constexpr auto fields = Reflect::Traits<T>::fields();
    std::apply([&](const auto&... field) { ((into.*(field.ptr) = from.*(field.ptr)), ...); }, fields);
    return into;
}

/**
 * @brief The undo step for a component edit on @p id.
 *
 * An override inside a prefab instance, else a ComponentEditCommand, which coalesces. An instance
 * root's Transform takes the plain edit, as PrefabOverrides::record declines it: the scene stores that pose.
 *
 * @tparam T Component type.
 * @param scene     Scene holding the entity.
 * @param resources Resolves asset handles to names.
 * @param id        Entity that was edited.
 * @param before    The component as it was before the edit.
 * @param after     The component as it is now.
 * @param label     History entry text; kept by pointer.
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
 * For an edit finished in one gesture; a drag that pushes once at the end takes editStep directly.
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
 * @brief Settle what an edit leaves to work out, on the live component.
 *
 * Call before the edit's step is cut, so the step holds the result. Nothing, but for the overloads below.
 *
 * @param component The component as edited.
 * @param resources Resolves what it names.
 */
template <typename T>
void settleEdit(T& component, const ResourceManager& resources) {
    (void)component;
    (void)resources;
}

/// Wakes the body so it does not sleep through the edit.
inline void settleEdit(Rigidbody& body, const ResourceManager&) {
    Rigidbody::wake(body);
}

/// A mesh part's triangles built for the mesh it now names.
inline void settleEdit(Collider& collider, const ResourceManager& resources) {
    syncMeshCollider(collider, resources);
}

/**
 * @brief The step for a Transform a clip's playhead posed.
 *
 * Outside a session the pose is the saved Transform, so it is recorded; in one it is session state,
 * and a step would drop the history Stop keeps.
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

/**
 * @brief The step for an edit that scaled @p id: its Transform's, and its Collider's, whose live parts
 *        the edit already carried to the new scale.
 *
 * The Collider is settled first, so the step holds a mesh part's triangles at its new scale.
 *
 * @param scene          Scene holding the entity, which has a Collider.
 * @param resources      Resolves asset handles.
 * @param id             The scaled entity.
 * @param before         Its Transform before the edit.
 * @param after          Its Transform now.
 * @param colliderBefore Its Collider before the edit.
 * @param label          History entry text; kept by pointer.
 * @return One step for both, so an undo puts them back together.
 */
inline std::unique_ptr<Command> scaledStep(
    Scene& scene,
    ResourceManager& resources,
    EntityId id,
    const Transform& before,
    const Transform& after,
    const Collider& colliderBefore,
    const char* label
) {
    Collider& collider = scene.get<Collider>(id);
    settleEdit(collider, resources);
    auto step = std::make_unique<CompositeCommand>(label);
    step->add(editStep<Transform>(scene, resources, id, before, after, label));
    step->add(editStep<Collider>(scene, resources, id, colliderBefore, collider, label));
    return step;
}

/**
 * @brief The step for an edit of @p id's Transform, which carries a scale the edit took into the
 *        entity's live Collider.
 *
 * Physics reads no Transform scale (collider.h), so the live parts take the scale's change
 * (rescaleParts) in the same step: a box's collider grows with the box.
 *
 * @param scene     Scene holding the entity.
 * @param resources Resolves asset handles.
 * @param id        The edited entity.
 * @param before    Its Transform before the edit.
 * @param after     Its Transform now, already applied.
 * @param label     History entry text; kept by pointer.
 * @return editStep's alone when the scale stayed or there is no Collider.
 */
inline std::unique_ptr<Command> transformStep(
    Scene& scene,
    ResourceManager& resources,
    EntityId id,
    const Transform& before,
    const Transform& after,
    const char* label
) {
    Collider* collider = scene.tryGet<Collider>(id);
    if (!collider || after.scale == before.scale) {
        return editStep<Transform>(scene, resources, id, before, after, label);
    }
    const Collider was = *collider;
    collider->parts = rescaleParts(was.parts, before.scale, after.scale);
    return scaledStep(scene, resources, id, before, after, was, label);
}

/**
 * @brief @p into with the fields of @p from that @p mask names; the rest stay its own.
 *
 * @param into The component taking them.
 * @param from The component giving them.
 * @param mask Bit i for reflected field i: the fields an edit's rows changed.
 * @return @p into, unchanged when T is not reflected.
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
 * One CompositeCommand, so a drag across several entities still merges into one step.
 *
 * @param scene     Scene holding the entities.
 * @param resources Resolves asset handles.
 * @param selection The selected entities; @p id is one of them.
 * @param id        The entity edited.
 * @param before    Its T before the edit.
 * @param after     Its T now, already applied.
 * @param label     History entry text; kept by pointer.
 * @param shared    Fields the others take, bit i for reflected field i; never a list or an entity field.
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
    const char* label,
    uint64_t shared
) {
    std::vector<EntityId> others;
    for (const EntityId other : selection) {
        if (other != id && scene.isAlive(other) && scene.has<T>(other)) others.push_back(other);
    }
    if (others.empty()) return editStep<T>(scene, resources, id, before, after, label);

    auto step = std::make_unique<CompositeCommand>(label);
    step->add(editStep<T>(scene, resources, id, before, after, label));
    if (shared == 0) return step;
    for (const EntityId other : others) {
        T& value    = scene.get<T>(other);
        const T was = value;
        value = withFields(value, after, shared);
        settleEdit(value, resources);
        step->add(editStep<T>(scene, resources, other, was, value, label));
    }
    return step;
}

/**
 * @brief @p other with what a Transform edit moved: position and scale per axis, and the turn taken.
 *
 * A one-axis move across a selection sets that axis alone, so nothing stacks; the turn is applied as a
 * delta, never by re-reading angles, which would flip one past a right angle.
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
 * @brief The step for an edit of @p id's Transform, carried to every other selected entity by the axes
 *        it moved (withMovedAxes), each one's scale into its own Collider (transformStep).
 *
 * One CompositeCommand, so a drag across several entities still merges into one step.
 *
 * @param scene     Scene holding the entities.
 * @param resources Resolves asset handles.
 * @param selection The selected entities; @p id is one of them.
 * @param id        The entity edited.
 * @param before    Its Transform before the edit.
 * @param after     Its Transform now, already applied.
 * @param label     History entry text; kept by pointer.
 * @return The step: transformStep's alone when no other selected entity holds a Transform.
 */
inline std::unique_ptr<Command> transformSelectionStep(
    Scene& scene,
    ResourceManager& resources,
    const std::vector<EntityId>& selection,
    EntityId id,
    const Transform& before,
    const Transform& after,
    const char* label
) {
    auto step = std::make_unique<CompositeCommand>(label);
    for (const EntityId other : selection) {
        if (other == id || !scene.isAlive(other) || !scene.has<Transform>(other)) continue;
        Transform& moved    = scene.get<Transform>(other);
        const Transform was = moved;
        moved = withMovedAxes(moved, before, after);
        step->add(transformStep(scene, resources, other, was, moved, label));
    }
    if (step->empty()) return transformStep(scene, resources, id, before, after, label);
    step->add(transformStep(scene, resources, id, before, after, label));
    return step;
}

/**
 * @brief A component held open for editing, its step pushed through @ref editStep when the scope closes.
 *
 *     EditScope<Mesh> mesh(scene, resources, state, id, "Assign Material");
 *     mesh->material = MaterialHandle{key};
 *
 * It pushes by default and @ref discard opts out, as a forgotten push loses an undo step silently. Not
 * for a drag, which takes @ref editStep directly. Nothing may add or remove a T while a scope is open:
 * that moves the storage it holds a reference into.
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
         * @param label     History entry text; kept by pointer, so it must outlive the pushed step.
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

        /// Close without a step, as for a canceled dialog; a write already made stays, without its undo.
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
