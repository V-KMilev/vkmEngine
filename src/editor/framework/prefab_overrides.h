#pragma once

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include <nlohmann/json.hpp>

#include "ecs/entity.h"
#include "ecs/component/prefab_instance.h"
#include "io/scene/component_serializer.h"

#include "framework/command.h"

namespace Engine {

class Scene;
class ResourceManager;
struct EditorState;

/**
 * @brief Where a prefab instance's per-field overrides come from.
 *
 * The engine applies overrides when it builds an instance; this is the half
 * that makes them. Editing a component on an entity that belongs to an
 * instance records the changed fields as overrides there and then, rather than
 * waiting for an explicit "make this an override" step: a scene stores an
 * instance as a reference and rebuilds its entities from the prefab, so an edit
 * that is not an override is not stored at all, and an explicit step would
 * silently discard everything done before someone pressed it.
 *
 * An override is addressed (uid, component, field) with the value stored as the
 * field's own serialized JSON, so the keys here are the scene serializer's and
 * not the inspector's labels - a component key that does not match what the
 * serializer writes produces an override the prefab can never resolve.
 */
namespace PrefabOverrides {

    /**
     * @brief The instance root @p id belongs to - itself, or an ancestor.
     *
     * Walking up rather than marking every descendant is what keeps the
     * prefab's own entities free of bookkeeping that could fall out of sync
     * with their root, which is how the scene serializer decides the same
     * question.
     *
     * @param scene Scene holding the entity.
     * @param id    Entity to resolve.
     * @return The instance root, or a default (invalid) EntityId when @p id is
     *         not part of an instance.
     */
    EntityId instanceRoot(const Scene& scene, EntityId id);

    /**
     * @brief The fields of @p component this instance overrides on @p id.
     *
     * @param scene     Scene holding the entity.
     * @param id        Entity inside (or the root of) an instance.
     * @param component Component key, as SceneSerializer writes it.
     * @return The overridden field keys, in the order they are stored.
     */
    std::vector<std::string> overriddenFields(const Scene& scene, EntityId id,
                                              const char* component);

    /**
     * @brief Make @p entries the instance's overrides for (@p uid, @p component).
     *
     * Every entry addressing that entity and component is replaced, and the
     * component is re-read from the prefab so the live value matches what a
     * reload of the scene would produce. Used by the undo command in both
     * directions, which is why it takes the entry set whole rather than a delta.
     *
     * The entity is named by its prefab uid because that is what survives the
     * instance being rebuilt; it is resolved to a live entity by walking @p root
     * for the one carrying it.
     *
     * @param scene     Scene holding the instance.
     * @param resources Resolves the prefab's asset names to handles.
     * @param root      Instance root carrying the override list.
     * @param uid       Prefab uid of the entity the entries address.
     * @param component Component key, as SceneSerializer writes it.
     * @param entries   The entries that should remain for that pair.
     * @return True when the component was re-read; false when the prefab could
     *         not answer for it, which leaves the value on screen stale.
     */
    bool apply(Scene& scene, ResourceManager& resources, EntityId root, uint32_t uid,
               const std::string& component, const std::vector<PrefabOverride>& entries);

    /**
     * @brief Record the fields that differ between @p before and @p after as
     *        overrides on the instance @p id belongs to.
     *
     * Only the fields the edit actually changed are recorded. The alternative -
     * comparing the component against the prefab's own value - would turn a
     * load that could not resolve an asset name into an override that bakes the
     * failure into the scene.
     *
     * The entries are written here and the undo step handed back rather than
     * pushed, because for an instance the value is the prefab patched by these
     * entries: undoing the edit means undoing the entry, and a gizmo drag over a
     * multi-selection needs that step inside the composite it pushes for the
     * whole motion.
     *
     * @param scene     Scene holding the entity.
     * @param resources Resolves asset names to handles.
     * @param id        Entity that was edited.
     * @param component Component key, as SceneSerializer writes it.
     * @param before    The component's serialized value before the edit.
     * @param after     The component's serialized value after it.
     * @param label     History entry text.
     * @return The undo step for the recorded override, or null when the edit is
     *         not an override at all - @p id is not part of an instance, it is
     *         the root's Transform, which is the instance's own pose, or the
     *         prefab does not define that component and so has no value for an
     *         override to differ from - and the caller should record the edit
     *         the way it normally would.
     */
    std::unique_ptr<Command> recordFields(Scene& scene, ResourceManager& resources, EntityId id,
                                          const char* component, const nlohmann::json& before,
                                          const nlohmann::json& after, const char* label);

    /**
     * @brief Drop one override and give the field back the prefab's value.
     *
     * Refused, with a toast and the entry left alone, when the prefab no longer
     * defines that component: there is no value to give back, and dropping the
     * entry regardless would leave the overridden number in place with nothing
     * recording that it was ever an override.
     *
     * @param scene     Scene holding the entity.
     * @param resources Resolves the prefab's asset names to handles.
     * @param state     Editor state whose command stack receives the step.
     * @param id        Entity the override addresses.
     * @param component Component key, as SceneSerializer writes it.
     * @param field     Field key within that component.
     */
    void revert(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id,
                const char* component, const std::string& field);

    /**
     * @brief Say that a component added to or removed from an instance lives in
     *        the prefab, not in the scene.
     *
     * Which components an entity carries is the prefab's answer for everything
     * inside an instance: the scene stores the instance as a reference and
     * rebuilds the subtree from the file, so a component added here is in the
     * prefab or it is nowhere. Both gestures stand, because writing the prefab
     * back is how one is authored - this is what says where the change lives.
     *
     * Does nothing for an entity outside an instance, which is every entity in
     * most scenes.
     *
     * @param scene     Scene holding the entity.
     * @param state     Editor state receiving the toast.
     * @param id        Entity the component was added to or removed from.
     * @param component Component name, as the user was shown it.
     * @param fate      What becomes of it, completing "'<component>' <fate>".
     */
    void warnComponentIsPrefabs(const Scene& scene, EditorState& state, EntityId id,
                                const char* component, const char* fate);

    /**
     * @brief The JSON key SceneSerializer writes @p T under.
     *
     * Specialized for the components the inspector edits field by field.
     * ScriptComponent is deliberately absent: it serializes as a single field
     * holding every behavior, so a delta on it would be the whole list rather
     * than one field.
     */
    template <typename T> inline constexpr const char* COMPONENT_KEY = nullptr;

    template <> inline constexpr const char* COMPONENT_KEY<Name>             = "Name";
    template <> inline constexpr const char* COMPONENT_KEY<Transform>        = "Transform";
    template <> inline constexpr const char* COMPONENT_KEY<Camera>           = "Camera";
    template <> inline constexpr const char* COMPONENT_KEY<Light>            = "Light";
    template <> inline constexpr const char* COMPONENT_KEY<Rigidbody>        = "Rigidbody";
    template <> inline constexpr const char* COMPONENT_KEY<Collider>         = "Collider";
    template <> inline constexpr const char* COMPONENT_KEY<Mesh>             = "Mesh";
    template <> inline constexpr const char* COMPONENT_KEY<LOD>              = "LOD";
    template <> inline constexpr const char* COMPONENT_KEY<Decal>            = "Decal";
    template <> inline constexpr const char* COMPONENT_KEY<ParticleEmitter>  = "ParticleEmitter";
    template <> inline constexpr const char* COMPONENT_KEY<IrradianceVolume> = "IrradianceVolume";
    template <> inline constexpr const char* COMPONENT_KEY<ReflectionProbe>  = "ReflectionProbe";
    template <> inline constexpr const char* COMPONENT_KEY<Animation>        = "Animation";
    template <> inline constexpr const char* COMPONENT_KEY<UICanvas>         = "UICanvas";
    template <> inline constexpr const char* COMPONENT_KEY<UIElement>        = "UIElement";
    template <> inline constexpr const char* COMPONENT_KEY<UIImage>          = "UIImage";
    template <> inline constexpr const char* COMPONENT_KEY<UIText>           = "UIText";
    template <> inline constexpr const char* COMPONENT_KEY<UIButton>         = "UIButton";

    /**
     * @brief Serialize a component the way the scene serializer would.
     *
     * Mesh, LOD and Decal reference assets by handle and resolve a name through
     * the manager; every other component writes itself.
     *
     * @tparam T Component type.
     * @param component Value to serialize.
     * @param resources Resolves asset handles to names.
     * @return The component's JSON object.
     */
    template <typename T>
    nlohmann::json serialize(const T& component, const ResourceManager& resources) {
        if constexpr (std::is_same_v<T, Mesh> || std::is_same_v<T, LOD> ||
                      std::is_same_v<T, Decal>) {
            return ComponentSerializer::save(component, resources);
        } else {
            return ComponentSerializer::save(component);
        }
    }

    /**
     * @brief Record a typed component edit, serializing both sides for
     *        recordFields.
     *
     * @tparam T Component type; its key comes from COMPONENT_KEY.
     * @param scene     Scene holding the entity.
     * @param resources Resolves asset handles to names.
     * @param id        Entity that was edited.
     * @param before    The component as it was before the edit.
     * @param after     The component as it is now.
     * @param label     History entry text.
     * @return The undo step, or null when the edit is not an override.
     */
    template <typename T>
    std::unique_ptr<Command> record(Scene& scene, ResourceManager& resources, EntityId id,
                                    const T& before, const T& after, const char* label) {
        static_assert(COMPONENT_KEY<T> != nullptr,
                      "component has no serializer key - add one to COMPONENT_KEY");
        return recordFields(scene, resources, id, COMPONENT_KEY<T>,
                            serialize(before, resources), serialize(after, resources), label);
    }

} // namespace PrefabOverrides

} // namespace Engine
