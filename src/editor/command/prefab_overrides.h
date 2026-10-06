#pragma once

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include <nlohmann/json.hpp>

#include "ecs/entity.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "io/scene/component_serializer.h"

#include "command/command.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;
class CommandHost;

/**
 * @brief Reading and writing the overrides a prefab instance carries.
 *
 * A scene stores an instance as a reference and rebuilds its entities from the
 * prefab, so an edit that is not recorded as an override is not stored at all.
 * Call sites do not choose: editStep in command/component_edit.h asks
 * @ref record first and falls back to a plain component command when it declines.
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
    std::vector<std::string> overriddenFields(const Scene& scene, EntityId id, const char* component);

    /**
     * @brief Make @p entries the instance's overrides for (@p uid, @p component).
     *
     * Every entry addressing that entity and component is replaced, and the
     * component is re-read from the prefab so the live value matches what a
     * reload of the scene would produce.
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
    bool apply(
        Scene& scene,
        ResourceManager& resources,
        EntityId root,
        uint32_t uid,
        const std::string& component,
        const std::vector<PrefabOverride>& entries
    );

    /**
     * @brief Record the fields that differ between @p before and @p after as
     *        overrides on the instance @p id belongs to.
     *
     * Only the fields the edit changed are recorded. Comparing the component
     * against the prefab's own value instead would turn a load that could not
     * resolve an asset name into an override baking that failure into the scene.
     *
     * The undo step is handed back rather than pushed, so a caller can put it
     * inside a composite step of its own.
     *
     * @param scene     Scene holding the entity.
     * @param resources Resolves asset names to handles.
     * @param id        Entity that was edited.
     * @param component Component key, as SceneSerializer writes it.
     * @param before    The component's serialized value before the edit.
     * @param after     The component's serialized value after it.
     * @param label     History entry text.
     * @return The undo step for the recorded override, or null when the edit is
     *         not an override at all - not part of an instance, the root's own
     *         Transform, or a component the prefab does not define - in which
     *         case the caller records the edit the way it normally would.
     */
    std::unique_ptr<Command> recordFields(
        Scene& scene,
        ResourceManager& resources,
        EntityId id,
        const char* component,
        const nlohmann::json& before,
        const nlohmann::json& after,
        const char* label
    );

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
     * @param host      Whose command stack receives the step.
     * @param id        Entity the override addresses.
     * @param component Component key, as SceneSerializer writes it.
     * @param field     Field key within that component.
     */
    void revert(
        Scene& scene,
        ResourceManager& resources,
        CommandHost& host,
        EntityId id,
        const char* component,
        const std::string& field
    );

    /**
     * @brief Say that a component added to or removed from an instance lives in
     *        the prefab, not in the scene.
     *
     * The scene rebuilds an instance's subtree from the file, so a component
     * added or removed here is in the prefab or it is nowhere.
     *
     * No-op outside an instance.
     *
     * @param scene     Scene holding the entity.
     * @param host      Receives the toast.
     * @param id        Entity the component was added to or removed from.
     * @param component Component name, as the user was shown it.
     * @param fate      What becomes of it, completing "'<component>' <fate>".
     */
    void warnComponentIsPrefabs(
        const Scene& scene,
        CommandHost& host,
        EntityId id,
        const char* component,
        const char* fate
    );

    /**
     * @brief What SceneSerializer writes @p T under, and what it needs to.
     *
     * All three expand from VKM_SCENE_COMPONENTS, which is where the scene
     * format's component list lives. The letter says which of
     * ComponentSerializer::save's three shapes the component takes, which is
     * what @ref serialize reads.
     */
    template <typename T> inline constexpr const char* COMPONENT_KEY       = nullptr;
    template <typename T> inline constexpr bool        REFERENCES_ASSETS   = false;
    template <typename T> inline constexpr bool        REFERENCES_ENTITIES = false;

#define VKM_OVERRIDE_KEY(Type, Key) \
    template <> inline constexpr const char* COMPONENT_KEY<Type> = Key;
#define VKM_OVERRIDE_KEY_R(Type, Key)   \
    VKM_OVERRIDE_KEY(Type, Key)         \
    template <> inline constexpr bool REFERENCES_ASSETS<Type> = true;
#define VKM_OVERRIDE_KEY_E(Type, Key)   \
    VKM_OVERRIDE_KEY(Type, Key)         \
    template <> inline constexpr bool REFERENCES_ENTITIES<Type> = true;
    VKM_SCENE_COMPONENTS(VKM_OVERRIDE_KEY, VKM_OVERRIDE_KEY_R, VKM_OVERRIDE_KEY_E)
#undef VKM_OVERRIDE_KEY_E
#undef VKM_OVERRIDE_KEY_R
#undef VKM_OVERRIDE_KEY

    /**
     * @brief Serialize a component the way the scene serializer would.
     *
     * Which of the three shapes it takes is the letter its row in
     * VKM_SCENE_COMPONENTS carries: R resolves asset names through the manager,
     * E names other entities, and everything else writes itself.
     *
     * @tparam T Component type.
     * @param component Value to serialize.
     * @param resources Resolves asset handles to names.
     * @return The component's JSON object.
     */
    template <typename T>
    nlohmann::json serialize(const T& component, const ResourceManager& resources) {
        if constexpr (REFERENCES_ASSETS<T>) {
            return ComponentSerializer::save(component, resources);
        } else if constexpr (REFERENCES_ENTITIES<T>) {
            // By slot, the scene file's own number, because that is the document
            // this value is stored in. Entity fields never reach an override -
            // recordFields declines them - so none is read back elsewhere.
            auto bySlot = [](EntityId e) { return e.slot(); };
            const EntityNamer name(bySlot);
            return ComponentSerializer::save(component, name);
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
    std::unique_ptr<Command> record(
        Scene& scene,
        ResourceManager& resources,
        EntityId id,
        const T& before,
        const T& after,
        const char* label
    ) {
        static_assert(
            COMPONENT_KEY<T> != nullptr,
            "component is not in the scene format - add a row to VKM_SCENE_COMPONENTS"
        );
        static_assert(
            !std::is_same_v<T, ScriptComponent>,
            "a script edit is one document, not a field delta - ScriptEditCommand covers it"
        );
        // Asked before either side is serialized: a drag comes through here
        // each frame, and outside an instance there is nothing to record.
        if (!instanceRoot(scene, id)) return nullptr;
        return recordFields(
            scene,
            resources,
            id,
            COMPONENT_KEY<T>,
            serialize(before, resources),
            serialize(after, resources),
            label
        );
    }

} // namespace PrefabOverrides

} // namespace Vkm::Engine
