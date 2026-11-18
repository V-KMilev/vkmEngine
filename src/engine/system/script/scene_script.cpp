#define VKM_LOG_CATEGORY "IO"

// The scene format's Script row (io/scene declares it): only the script system can read behaviors.

#include "io/scene/component_serializer.h"
#include "io/scene/asset_refs.h"

#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

#include "vkm_log/logger.h"

#include "base/reflect/field_info.h"
#include "io/json/reflect_json.h"
#include "resource/resource_manager.h"
#include "system/script/behavior_base.h"
#include "system/script/behavior_field_visitor.h"
#include "system/script/behavior_registry.h"
#include "system/script/script_component.h"

namespace Vkm::Engine::ComponentSerializer {

namespace {

using ReflectJson::leafFromJson;
using ReflectJson::leafToJson;

/**
 * @brief Writes each visited reflected field into a JSON object.
 *
 * Read-only on the behavior (see the const_cast note in save()). A nested struct
 * becomes a sub-object, pushed by beginStruct and popped by endStruct.
 */
class BehaviorJsonWriter : public BehaviorFieldVisitor {
    public:
        explicit BehaviorJsonWriter(nlohmann::json& out) { m_scopes.push_back(&out); }
        ~BehaviorJsonWriter() override = default;

        BehaviorJsonWriter(const BehaviorJsonWriter& other) = delete;
        BehaviorJsonWriter& operator=(const BehaviorJsonWriter& other) = delete;

        BehaviorJsonWriter(BehaviorJsonWriter && other) = delete;
        BehaviorJsonWriter& operator=(BehaviorJsonWriter && other) = delete;

    public:
        void field(const FieldInfo& info, float& v)       override { cur()[info.name] = leafToJson(v); }
        void field(const FieldInfo& info, int& v)         override { cur()[info.name] = leafToJson(v); }
        void field(const FieldInfo& info, bool& v)        override { cur()[info.name] = leafToJson(v); }
        void field(const FieldInfo& info, glm::vec2& v)   override { cur()[info.name] = leafToJson(v); }
        void field(const FieldInfo& info, glm::vec3& v)   override { cur()[info.name] = leafToJson(v); }
        void field(const FieldInfo& info, glm::vec4& v)   override { cur()[info.name] = leafToJson(v); }
        void field(const FieldInfo& info, glm::quat& v)   override { cur()[info.name] = leafToJson(v); }
        void field(const FieldInfo& info, std::string& v) override { cur()[info.name] = leafToJson(v); }

        void enumField(
            const FieldInfo& info,
            int& index,
            const char* const* names,
            std::size_t count
        ) override {
            // By name, not index, so reordering enum values keeps scenes valid
            // (matches Reflect::enumName).
            if (index >= 0 && static_cast<std::size_t>(index) < count) cur()[info.name] = names[index];
        }

        // Written as its name; which assets-block section it belongs in is
        // SceneAssets::collectAssetRefs's to say.
        void assetField(const FieldInfo& info, std::string& assetName, AssetType type) override {
            field(info, assetName);
        }

        // As a plain path, as a bare string field wrote it.
        void prefabField(const FieldInfo& info, std::string& path) override { field(info, path); }

        bool beginStruct(const FieldInfo& info) override {
            m_scopes.push_back(&(cur()[info.name] = nlohmann::json::object()));
            return true;
        }
        void endStruct() override { m_scopes.pop_back(); }

    private:
        // json objects are std::map-backed, so these pointers survive sibling inserts.
        nlohmann::json& cur() { return *m_scopes.back(); }

    private:
        std::vector<nlohmann::json*> m_scopes;
};

/**
 * @brief Reads each visited reflected field from a JSON object.
 *
 * A missing or malformed key keeps the field's current value; so does a missing
 * or mistyped sub-object (beginStruct returns false).
 */
class BehaviorJsonReader : public BehaviorFieldVisitor {
    public:
        explicit BehaviorJsonReader(const nlohmann::json& in) { m_scopes.push_back(&in); }
        ~BehaviorJsonReader() override = default;

        BehaviorJsonReader(const BehaviorJsonReader& other) = delete;
        BehaviorJsonReader& operator=(const BehaviorJsonReader& other) = delete;

        BehaviorJsonReader(BehaviorJsonReader && other) = delete;
        BehaviorJsonReader& operator=(BehaviorJsonReader && other) = delete;

    public:
        void field(const FieldInfo& info, float& v)       override { read(info.name, v); }
        void field(const FieldInfo& info, int& v)         override { read(info.name, v); }
        void field(const FieldInfo& info, bool& v)        override { read(info.name, v); }
        void field(const FieldInfo& info, glm::vec2& v)   override { read(info.name, v); }
        void field(const FieldInfo& info, glm::vec3& v)   override { read(info.name, v); }
        void field(const FieldInfo& info, glm::vec4& v)   override { read(info.name, v); }
        void field(const FieldInfo& info, glm::quat& v)   override { read(info.name, v); }
        void field(const FieldInfo& info, std::string& v) override { read(info.name, v); }

        void assetField(const FieldInfo& info, std::string& assetName, AssetType type) override {
            field(info, assetName);
        }

        void prefabField(const FieldInfo& info, std::string& path) override { field(info, path); }

        void enumField(
            const FieldInfo& info,
            int& index,
            const char* const* names,
            std::size_t count
        ) override {
            const nlohmann::json* node = present(info.name);
            if (!node) return;
            if (!node->is_string()) {
                keepCurrent(info.name, *node, "not a value's name");
                return;
            }
            const std::string picked = node->get<std::string>();
            for (std::size_t i = 0; i < count; ++i) {
                if (picked == names[i]) {
                    index = static_cast<int>(i);
                    return;
                }
            }
            // A removed or renamed value.
            keepCurrent(info.name, *node, "no value this build has");
        }

        bool beginStruct(const FieldInfo& info) override {
            if (!cur().contains(info.name) || !cur()[info.name].is_object()) return false;   // keep current
            m_scopes.push_back(&cur()[info.name]);
            return true;
        }
        void endStruct() override { m_scopes.pop_back(); }

    private:
        const nlohmann::json& cur() { return *m_scopes.back(); }

        // The node @p name holds in the current scope; null when absent, which
        // keeps the field's value silently.
        const nlohmann::json* present(const char* name) {
            const auto it = cur().find(name);
            return it == cur().end() ? nullptr : &*it;
        }

        // A wrong-typed value keeps the field's current value with a warning: a throw out of here
        // would cost the whole load rather than one value.
        template<typename T>
        void read(const char* name, T& v) {
            const nlohmann::json* node = present(name);
            if (!node) return;
            try {
                leafFromJson(*node, v);
            } catch (const nlohmann::json::exception& e) {
                keepCurrent(name, *node, e.what());
            }
        }

        static void keepCurrent(const char* name, const nlohmann::json& got, const char* why) {
            LOG_WARNING(
                "Behavior field '%s' holds '%s' (%s); keeping its current value",
                name,
                got.dump().c_str(),
                why
            );
        }

    private:
        std::vector<const nlohmann::json*> m_scopes;
};

} // namespace

nlohmann::json save(const ScriptComponent& sc) {
    nlohmann::json behaviors = nlohmann::json::array();

    // A held one goes back exactly as read, its module missing; none is dropped. Order is run order, so
    // it goes back behind as many constructed ones as were listed ahead of it, held ones that share a
    // place in the order they are listed.
    std::vector<const UnknownBehavior*> held;
    held.reserve(sc.unknown.size());
    for (const UnknownBehavior& kept : sc.unknown) held.push_back(&kept);
    const auto runsEarlier = [](const UnknownBehavior* a, const UnknownBehavior* b) {
        return a->constructedBefore < b->constructedBefore;
    };
    std::stable_sort(held.begin(), held.end(), runsEarlier);
    size_t next = 0;
    const auto emitHeld = [&](const UnknownBehavior& kept) {
        nlohmann::json props = nlohmann::json::parse(kept.properties, nullptr, false);
        if (props.is_discarded()) props = nlohmann::json::object();
        behaviors.push_back({{"type", kept.type}, {"properties", std::move(props)}});
    };

    for (size_t at = 0; at < sc.behaviors.size(); ++at) {
        while (next < held.size() && held[next]->constructedBefore <= at) emitHeld(*held[next++]);
        const std::unique_ptr<BehaviorBase>& behavior = sc.behaviors[at];
        if (!behavior) continue;
        nlohmann::json props = nlohmann::json::object();
        BehaviorJsonWriter writer(props);
        // The writer only reads, so casting away const for visitFields is safe.
        const_cast<BehaviorBase&>(*behavior).visitFields(writer);
        behaviors.push_back({{"type", behavior->typeName()}, {"properties", std::move(props)}});
    }
    while (next < held.size()) emitHeld(*held[next++]);
    return {{"behaviors", std::move(behaviors)}};
}

void load(const nlohmann::json& j, ScriptComponent& sc) {
    sc.behaviors.clear();
    sc.unknown.clear();
    if (!j.contains("behaviors") || !j["behaviors"].is_array()) return;
    for (const auto& entry : j["behaviors"]) {
        const std::string type = entry.value("type", std::string{});
        if (type.empty()) continue;

        const bool hasProperties    = entry.contains("properties") && entry["properties"].is_object();
        const nlohmann::json* props = hasProperties ? &entry["properties"] : nullptr;

        auto behavior = BehaviorRegistry::get().create(type);
        if (!behavior) {
            // Kept: an unregistered type means the module is absent, not that
            // the scene stopped wanting the behavior. See UnknownBehavior.
            std::string properties = props ? props->dump() : std::string{"{}"};
            sc.unknown.push_back({type, std::move(properties), sc.behaviors.size()});
            continue;
        }
        if (props) {
            BehaviorJsonReader reader(*props);
            behavior->visitFields(reader);
        }
        sc.behaviors.push_back(std::move(behavior));
    }
}

void unreadBehaviorFields(const nlohmann::json& script, std::set<std::string>& out) {
    const auto behaviors = script.find("behaviors");
    if (behaviors == script.end() || !behaviors->is_array()) return;
    for (const nlohmann::json& entry : *behaviors) {
        const auto type       = entry.find("type");
        const auto properties = entry.find("properties");
        if (type == entry.end() || !type->is_string() || properties == entry.end()) continue;

        // One no module registered is held as text, every key kept; load() has said it is missing.
        const std::string name = type->get<std::string>();
        if (!BehaviorRegistry::get().contains(name)) continue;
        nlohmann::json written = nlohmann::json::object();
        BehaviorJsonWriter writer(written);
        BehaviorRegistry::get().create(name)->visitFields(writer);
        ReflectJson::unreadKeys(*properties, written, "Script." + name, out);
    }
}

} // namespace Vkm::Engine::ComponentSerializer

namespace Vkm::Engine::SceneAssets {

namespace {

/**
 * @brief Collects (kind, name) for every asset a behavior's authored fields name.
 *
 * Only assetField collects. beginStruct returns true: an AssetRef nested in a reflected struct is as
 * much a reference as one at the top level.
 */
class BehaviorAssetRefs : public BehaviorFieldVisitor {
    public:
        /**
         * @brief Collect into @p refs, after whatever it already holds.
         *
         * @param refs The list each reference is appended to.
         */
        explicit BehaviorAssetRefs(std::vector<std::pair<AssetType, std::string>>& refs)
            : m_refs(refs) {}
        ~BehaviorAssetRefs() override = default;

        BehaviorAssetRefs(const BehaviorAssetRefs& other) = delete;
        BehaviorAssetRefs& operator=(const BehaviorAssetRefs& other) = delete;

        BehaviorAssetRefs(BehaviorAssetRefs && other) = delete;
        BehaviorAssetRefs& operator=(BehaviorAssetRefs && other) = delete;

    public:
        void field(const FieldInfo& info, float& value)       override {}
        void field(const FieldInfo& info, int& value)         override {}
        void field(const FieldInfo& info, bool& value)        override {}
        void field(const FieldInfo& info, glm::vec2& value)   override {}
        void field(const FieldInfo& info, glm::vec3& value)   override {}
        void field(const FieldInfo& info, glm::vec4& value)   override {}
        void field(const FieldInfo& info, glm::quat& value)   override {}
        void field(const FieldInfo& info, std::string& value) override {}

        void enumField(
            const FieldInfo& info,
            int& index,
            const char* const* names,
            std::size_t count
        ) override {}

        void assetField(const FieldInfo& info, std::string& assetName, AssetType type) override {
            if (assetName.empty()) return;
            m_refs.emplace_back(type, assetName);
        }

        // A prefab's own file names its assets; a scene's block does not list them.
        void prefabField(const FieldInfo& info, std::string& path) override {}

        bool beginStruct(const FieldInfo& info) override { return true; }
        void endStruct() override {}

    private:
        std::vector<std::pair<AssetType, std::string>>& m_refs;
};

/// Each string in @p node, at any depth, that names an asset @p resources holds, as each kind holding one.
void collectHeldNames(
    const nlohmann::json& node,
    const ResourceManager& resources,
    std::vector<std::pair<AssetType, std::string>>& names
) {
    if (node.is_object() || node.is_array()) {
        for (const nlohmann::json& item : node) collectHeldNames(item, resources, names);
        return;
    }
    if (!node.is_string()) return;
    const std::string& name = node.get_ref<const std::string&>();
#define VKM_HELD_NAME(tag, Asset, label, dir, cooked) \
    if (resources.findByName<Asset>(name)) names.emplace_back(AssetType::tag, name);
    VKM_ASSET_KINDS(VKM_HELD_NAME)
#undef VKM_HELD_NAME
}

} // namespace

void collectBehaviorAssetRefs(
    const ScriptComponent& script,
    const ResourceManager& resources,
    std::vector<std::pair<AssetType, std::string>>& names
) {
    // A behavior names assets in authored fields, not handles, so only visitFields can see them.
    BehaviorAssetRefs behaviorRefs(names);
    for (const auto& behavior : script.behaviors) {
        if (!behavior) continue;
        // visitFields is non-const because a visitor may write; this one only reads, and a const
        // unique_ptr still hands out a mutable referent.
        behavior->visitFields(behaviorRefs);
    }
    // Saved while its module is missing, a behavior held as text still names its assets.
    for (const UnknownBehavior& held : script.unknown) {
        collectHeldNames(nlohmann::json::parse(held.properties, nullptr, false), resources, names);
    }
}

} // namespace Vkm::Engine::SceneAssets
