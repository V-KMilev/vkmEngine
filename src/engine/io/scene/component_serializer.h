#pragma once

#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "base/reflect/reflect.h"
#include "io/scene/entity_mapping.h"
#include "ecs/settings/environment.h"
#include "io/json/reflect_json.h"
#include "resource/asset_type.h"
#include "io/scene/scene_components.h"

namespace Vkm::Engine {

class Scene;

class ResourceManager;

/**
 * @brief Per-component (de)serialization to JSON.
 *
 * A reflected component writes its reflected fields and nothing else (ReflectJson), so one that
 * persists less than it holds - session state, a cache - leaves those fields out of its reflect
 * block. Templates serve every such row; an overload of its own, which wins over them, is for a
 * component that names entities or is not reflected. Asset handles are stored by name
 * (ResourceManager::findByName); Hierarchy::parent as the saved slot, which resolves directly
 * because SceneSerializer recreates each entity at its saved slot.
 */
namespace ComponentSerializer {

    /**
     * @brief An asset name a load failed to resolve, and the field it was read from.
     *
     * The field is its path in the scene format (ReflectJson::childPath), so the caller can
     * put the name back.
     */
    struct UnresolvedRef {
        std::string field;
        std::string name;
        AssetType   type = AssetType::Count;  ///< Which kind the loader looked it up as.
    };

    /**
     * @brief A reflected value that names no assets: a P row, or a scene's settings block.
     *
     * @param value What is written.
     * @return Its reflected fields, by name.
     */
    template <typename T>
    nlohmann::json save(const T& value) {
        static_assert(Reflect::IS_REFLECTED<T>, "a component with no reflect block has a save of its own");
        return ReflectJson::saveFields(value, ReflectJson::PlainData{});
    }

    /**
     * @brief Read what save(const T&) wrote; a key missing keeps the field's value.
     *
     * @param json  As written.
     * @param value Read into.
     */
    template <typename T>
    void load(const nlohmann::json& json, T& value) {
        static_assert(Reflect::IS_REFLECTED<T>, "a component with no reflect block has a load of its own");
        ReflectJson::loadFields(json, value, ReflectJson::PlainData{});
    }

    /**
     * @brief A reflected R row: each handle written as its asset's name.
     *
     * Instantiated for the R rows of VKM_SCENE_COMPONENTS.
     *
     * @param value     What is written.
     * @param resources Names the handles.
     * @return Its reflected fields, by name.
     */
    template <typename T>
    nlohmann::json save(const T& value, const ResourceManager& resources);

    /**
     * @brief Read what save(const T&, const ResourceManager&) wrote.
     *
     * A name no asset answers to leaves its handle empty, and the name is its only record: a save
     * without it writes an empty string over it.
     *
     * @param json       As written.
     * @param value      Read into.
     * @param resources  Resolves the names.
     * @param unresolved Each name that did not resolve is appended to it, for a caller that knows
     *                   the entity and component to keep (SceneSerializer::loadComponents).
     */
    template <typename T>
    void load(
        const nlohmann::json& json,
        T& value,
        const ResourceManager& resources,
        std::vector<UnresolvedRef>& unresolved
    );

    /// Clamps a fog anisotropy it cannot draw, with a warning.
    void load(const nlohmann::json&, Environment&);

    /// Builds a mesh part's triangles from its mesh, which are never written.
    void load(const nlohmann::json&, Collider&, const ResourceManager&, std::vector<UnresolvedRef>&);

    /**
     * @brief Joint: the reflected fields, and `connected` as the carrier's name for it.
     */
    nlohmann::json save(const Joint&, const EntityNamer&);
    void load(const nlohmann::json&, Joint&, const EntityResolver&);

    /**
     * @brief Ragdoll: the switch, the group node, and the bone mapping.
     *
     * The bones are entities the scene saves anyway; this carries which body poses
     * which bone and the offset between them, naming entities as the carrier does.
     */
    nlohmann::json save(const Ragdoll&, const EntityNamer&);
    void load(const nlohmann::json&, Ragdoll&, const EntityResolver&);

    nlohmann::json save(const UISlider&, const EntityNamer&);
    void load(const nlohmann::json&, UISlider&, const EntityResolver&);

    nlohmann::json save(const UIToggle&, const EntityNamer&);
    void load(const nlohmann::json&, UIToggle&, const EntityResolver&);

    nlohmann::json save(const UIChoice&, const EntityNamer&);
    void load(const nlohmann::json&, UIChoice&, const EntityResolver&);

    /**
     * @brief Hierarchy: only `parent` is serialized, as the parent's slot.
     *
     * Sibling links are rebuilt on load by HierarchyOperations::setParent.
     */
    nlohmann::json save(const Hierarchy&);
    /**
     * @brief Read a saved Hierarchy's parent reference as a saved slot.
     *
     * The caller links it once every entity exists at its saved slot. A root is
     * saved as slot 0, which no entity occupies; a block without the key yields
     * uint32_t max. Both read as a root.
     *
     * @param json Hierarchy object written by save(const Hierarchy&).
     * @return The parent's saved slot; 0 or uint32_t max for a root.
     */
    uint32_t loadParentIndex(const nlohmann::json& json);

    /**
     * @brief Animation: the three tracks, each with its easing by name, and playback state.
     */
    nlohmann::json save(const Animation&);
    void load(const nlohmann::json&, Animation&);

    /**
     * @brief ScriptComponent: each behavior by BehaviorRegistry type name, with
     *        its BehaviorBase::visitFields `properties`.
     *
     * An unregistered type is kept as an UnknownBehavior so the next save writes
     * it back; a field whose key is absent keeps its default. Defined by the script system
     * (system/script/scene_script.cpp), the one that can read a behavior.
     */
    nlohmann::json save(const ScriptComponent&);
    void load(const nlohmann::json&, ScriptComponent&);

    /**
     * @brief Each key of a behavior's `properties` in @p script that no field of its type reads, as
     *        "Script.<type>.<key>", into @p out.
     *
     * A field is known where a save of a default one writes it. A behavior of a type no module
     * registered is held as text, every key kept, so names none.
     *
     * @param script The Script block, as save(const ScriptComponent&) writes it.
     * @param out    Receives each key's path.
     */
    void unreadBehaviorFields(const nlohmann::json& script, std::set<std::string>& out);

} // namespace ComponentSerializer

} // namespace Vkm::Engine
