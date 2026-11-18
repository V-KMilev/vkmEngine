#pragma once

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include <nlohmann/json.hpp>

#include "ecs/entity.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "io/scene/component_serializer.h"
#include "io/scene/scene_components.h"
#include "io/scene/prefab.h"
#include "ecs/prefab_instances.h"

#include "undo/command.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;
class CommandHost;

/**
 * @brief Change what one instance overrides on one component, undoable.
 *
 * Stands in for ComponentEditCommand inside an instance, whose value is the prefab's patched by the
 * overrides: the override entries are what is reversed, carried whole per (entity, component).
 */
class PrefabOverrideCommand : public Command {
    public:
        /**
         * @brief Record an override change that has already been applied.
         *
         * The entity is named by prefab uid: a rebuild pins only the root's slot (Prefab::instantiateInto).
         *
         * @param resources Manager the re-read resolves the prefab's assets against.
         * @param root      Instance root carrying the override list.
         * @param targetUid Prefab uid of the entity the entries address.
         * @param component Component key, as SceneSerializer writes it.
         * @param before    Entries for that pair before the edit.
         * @param after     Entries for it now.
         * @param label     History entry text.
         */
        PrefabOverrideCommand(
            ResourceManager& resources,
            EntityId root,
            uint32_t targetUid,
            std::string component,
            std::vector<PrefabOverride> before,
            std::vector<PrefabOverride> after,
            const char* label
        )
            : m_resources(&resources)
            , m_root(root)
            , m_targetUid(targetUid)
            , m_component(std::move(component))
            , m_before(std::move(before))
            , m_after(std::move(after))
            , m_label(label)
        {}
        ~PrefabOverrideCommand() override = default;

        PrefabOverrideCommand(const PrefabOverrideCommand& other) = delete;
        PrefabOverrideCommand& operator=(const PrefabOverrideCommand& other) = delete;

        PrefabOverrideCommand(PrefabOverrideCommand && other) = delete;
        PrefabOverrideCommand& operator=(PrefabOverrideCommand && other) = delete;

    public:
        void redo(Scene&, CommandHost&) override;
        void undo(Scene&, CommandHost&) override;
        const char* label() const override { return m_label; }
        bool tryMerge(Command& incoming) override;
        bool addresses(uint32_t slotIndex) const override { return m_root.slot() == slotIndex; }

    private:
        /**
         * @brief Install @p entries for this pair, warning when the prefab cannot answer.
         *
         * @param scene Scene holding the instance.
         * @param host  Receives the warning.
         * @param entries The entry set this direction installs.
         */
        void step(Scene& scene, CommandHost& host, const std::vector<PrefabOverride>& entries);

    private:
        ResourceManager*            m_resources;
        EntityId                    m_root;
        uint32_t                    m_targetUid;
        std::string                 m_component;
        std::vector<PrefabOverride> m_before;
        std::vector<PrefabOverride> m_after;
        const char*                 m_label;
};

/**
 * @brief Reading and writing the overrides a prefab instance carries.
 *
 * An instance is rebuilt from its prefab, so an edit not recorded as an override is not stored;
 * editStep in undo/component_edit.h asks @ref record first. Component and field keys are the scene
 * serializer's, never the inspector's labels, or the prefab can never resolve the override.
 */
namespace PrefabOverrides {

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
     * @brief Make @p entries the overrides for (@p uid, @p component), re-reading it as a reload would.
     *
     * @param scene     Scene holding the instance.
     * @param resources Resolves the prefab's asset names to handles.
     * @param root      Instance root carrying the override list.
     * @param uid       Prefab uid of the entity the entries address.
     * @param component Component key, as SceneSerializer writes it.
     * @param entries   The entries that should remain for that pair.
     * @return False when the prefab could not answer for it, which leaves the value on screen stale.
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
     * @brief Record the fields that differ between @p before and @p after as overrides on @p id's instance.
     *
     * Only the fields the edit changed: a diff against the prefab's value would bake an asset name a load
     * failed to resolve into the scene. The step is returned, not pushed, so a caller can compose it.
     *
     * @param scene     Scene holding the entity.
     * @param resources Resolves asset names to handles.
     * @param id        Entity that was edited.
     * @param component Component key, as SceneSerializer writes it.
     * @param before    The component's serialized value before the edit.
     * @param after     The component's serialized value after it.
     * @param entities  Fields naming entities (entityFields()), never overrides: an override is read in
     *                  the prefab's numbering, an edit names the scene's.
     * @param label     History entry text.
     * @return The undo step, or null outside an instance, for the root's own Transform, or for a
     *         component the prefab lacks; the caller then records the edit as it normally would.
     */
    std::unique_ptr<Command> recordFields(
        Scene& scene,
        ResourceManager& resources,
        EntityId id,
        const char* component,
        const nlohmann::json& before,
        const nlohmann::json& after,
        const std::vector<std::string>& entities,
        const char* label
    );

    /**
     * @brief Drop one override as an undo step, giving the field back the prefab's value.
     *
     * Refused with a toast when the prefab no longer defines the component: there is no value to give
     * back, and dropping the entry would leave the overridden value with no record of it.
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
     * @brief Revert every override of @p component on @p id as one undo step; refused as revert() is.
     *
     * @param scene     Scene holding the entity.
     * @param resources Resolves the prefab's asset names to handles.
     * @param host      Whose command stack receives the step.
     * @param id        Entity the overrides address.
     * @param component Component key, as SceneSerializer writes it.
     */
    void revertComponent(
        Scene& scene,
        ResourceManager& resources,
        CommandHost& host,
        EntityId id,
        const char* component
    );

    /**
     * @brief Revert every override on the instance @p root as one undo step.
     *
     * Those on a component the prefab no longer defines are kept, with a toast.
     *
     * @param scene     Scene holding the instance.
     * @param resources Resolves the prefab's asset names to handles.
     * @param host      Whose command stack receives the step.
     * @param root      The instance root; any other entity reverts nothing.
     */
    void revertAll(Scene& scene, ResourceManager& resources, CommandHost& host, EntityId root);

    /**
     * @brief Toast that a component added to or removed from an instance is the prefab's; no-op elsewhere.
     *
     * The scene rebuilds an instance's subtree from the file, so the component is in the prefab or nowhere.
     *
     * @param scene     Scene holding the entity.
     * @param host      Receives the toast.
     * @param id        Entity the component was added to or removed from.
     * @param component Name as the user was shown it.
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
     * @brief What SceneSerializer writes @p T under, and what its save needs.
     *
     * All three expand from VKM_SCENE_COMPONENTS, the scene format's component list; a row's letter
     * picks which of ComponentSerializer::save's three shapes @ref serialize takes.
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
     * The letter on its VKM_SCENE_COMPONENTS row picks the shape: R resolves asset names through the
     * manager, E names other entities, and everything else writes itself.
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
            // By slot, the scene file's numbering; recordFields declines entity fields,
            // so none is read back elsewhere.
            auto bySlot = [](EntityId e) { return e.slot(); };
            const EntityNamer name(bySlot);
            return ComponentSerializer::save(component, name);
        } else {
            return ComponentSerializer::save(component);
        }
    }

    /**
     * @brief The fields of @p component that name entities: those a second numbering writes differently.
     *
     * @tparam T Component type.
     * @param component Value whose fields are asked.
     * @return The field keys; none for a component whose row names no entities.
     */
    template <typename T>
    std::vector<std::string> entityFields(const T& component) {
        std::vector<std::string> fields;
        if constexpr (REFERENCES_ENTITIES<T>) {
            auto bySlot  = [](EntityId e) { return e.slot(); };
            auto shifted = [](EntityId e) { return e.slot() + 1u; };
            const nlohmann::json one = ComponentSerializer::save(component, EntityNamer(bySlot));
            const nlohmann::json two = ComponentSerializer::save(component, EntityNamer(shifted));
            for (auto field = one.begin(); field != one.end(); ++field) {
                if (two.value(field.key(), nlohmann::json()) != field.value()) fields.push_back(field.key());
            }
        }
        return fields;
    }

    /**
     * @brief Record a typed component edit, serializing both sides for recordFields().
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
        if (!Prefab::instanceRootOf(scene, id)) return nullptr;
        // Either side may name an entity the other does not.
        std::vector<std::string> entities = entityFields(before);
        for (std::string& field : entityFields(after)) entities.push_back(std::move(field));
        return recordFields(
            scene,
            resources,
            id,
            COMPONENT_KEY<T>,
            serialize(before, resources),
            serialize(after, resources),
            entities,
            label
        );
    }

} // namespace PrefabOverrides

} // namespace Vkm::Engine
