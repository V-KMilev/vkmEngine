#define VKM_LOG_CATEGORY "IO"

#include "io/scene/component_serializer.h"

#include <cstddef>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

#include "logger.h"

#include "ecs/component/camera.h"
#include "ecs/component/transform.h"
#include "io/json_vec.h"
#include "core/reflect.h"
#include "resource/resource_manager.h"
#include "system/script/behavior.h"
#include "system/script/behavior_field_visitor.h"
#include "system/script/behavior_registry.h"

namespace Engine::ComponentSerializer {

namespace {

// vec/quat <-> JSON helpers are shared with asset_serializer; see io/json_vec.h.
using ::Engine::detail::vec2ToJson;
using ::Engine::detail::vec3ToJson;
using ::Engine::detail::vec4ToJson;
using ::Engine::detail::quatToJson;
using ::Engine::detail::jsonToVec2;
using ::Engine::detail::jsonToVec3;
using ::Engine::detail::jsonToVec4;
using ::Engine::detail::jsonToQuat;

// toJson / fromJson overload set. The reflection-driven save/load templates
// (saveReflected / loadReflected) iterate a type's fields and forward each
// to these helpers; adding a new field type means adding a new pair here.
// No ADL is required: the templates live in this anon namespace and so do
// every overload, so unqualified lookup at the saveReflected definition
// already covers everything.

// Primitives.
inline nlohmann::json toJson(bool        v) { return v; }
inline nlohmann::json toJson(int         v) { return v; }
inline nlohmann::json toJson(uint32_t    v) { return v; }
inline nlohmann::json toJson(float       v) { return v; }
inline nlohmann::json toJson(const std::string& s) { return s; }
inline nlohmann::json toJson(const glm::vec2& v) { return vec2ToJson(v); }
inline nlohmann::json toJson(const glm::vec3& v) { return vec3ToJson(v); }
inline nlohmann::json toJson(const glm::vec4& v) { return vec4ToJson(v); }
inline nlohmann::json toJson(const glm::quat& q) { return quatToJson(q); }

inline void fromJson(const nlohmann::json& j, bool&     v) { v = j.get<bool>(); }
inline void fromJson(const nlohmann::json& j, int&      v) { v = j.get<int>(); }
inline void fromJson(const nlohmann::json& j, uint32_t& v) { v = j.get<uint32_t>(); }
inline void fromJson(const nlohmann::json& j, float&    v) { v = j.get<float>(); }
inline void fromJson(const nlohmann::json& j, std::string& s) { s = j.get<std::string>(); }
inline void fromJson(const nlohmann::json& j, glm::vec2& v) { v = jsonToVec2(j, v); }
inline void fromJson(const nlohmann::json& j, glm::vec3& v) { v = jsonToVec3(j, v); }
inline void fromJson(const nlohmann::json& j, glm::vec4& v) { v = jsonToVec4(j, v); }
inline void fromJson(const nlohmann::json& j, glm::quat& q) { q = jsonToQuat(j, q); }

// Fixed-size character buffers (e.g. Name::value is char[64]).
template<std::size_t N>
inline nlohmann::json toJson(const char (&v)[N]) { return std::string(v); }
template<std::size_t N>
inline void fromJson(const nlohmann::json& j, char (&v)[N]) {
    const std::string s = j.get<std::string>();
    std::strncpy(v, s.c_str(), N - 1);
    v[N - 1] = '\0';
}

// Enums: any scoped enum registered via VKM_ENUM_NAMES (Camera's ProjectionType,
// Light's LightType, ...). The SFINAE keeps these out of overload resolution for
// non-enum types, so the primitive overloads above still win for those.
template<typename E, typename = std::enable_if_t<std::is_enum_v<E>>>
inline nlohmann::json toJson(E v) { return Reflect::enumName(v); }
template<typename E, typename = std::enable_if_t<std::is_enum_v<E>>>
inline void fromJson(const nlohmann::json& j, E& v) { v = Reflect::enumFromName<E>(j.get<std::string>()); }

// Reflection driver. Iterates a type's reflected fields and forwards each
// (name, value) through the toJson / fromJson overload set. Phase-1
// unqualified lookup at this definition site picks up everything declared
// above; new field types only need a matching overload, no template tweak.

template<typename T>
nlohmann::json saveReflected(const T& obj) {
    nlohmann::json out = nlohmann::json::object();
    ::Engine::Reflect::forEachField(obj, [&](std::string_view name, const auto& val) {
        out[std::string(name)] = toJson(val);
    });
    return out;
}

template<typename T>
void loadReflected(const nlohmann::json& j, T& obj) {
    ::Engine::Reflect::forEachField(obj, [&](std::string_view name, auto& val) {
        auto it = j.find(std::string(name));
        if (it != j.end()) fromJson(*it, val);
    });
}

} // namespace

// Component save / load. Reflectable components are one-line passthroughs
// into the reflection driver. The exceptions are intentional:
//   - Mesh:      ResourceManager handle lookup (cross-asset reference).
//   - Hierarchy: parent stored as raw scene-table index, resolved by
//                SceneSerializer after the entity table is loaded.
//   - Animation: AnimationTrack<T> keeps its keyframes private and is driven
//                through explicit accessors (getTimes / getValues / getEasing
//                on save; clear / setEasing / addKeyframe on load), and
//                updateDuration() must be re-derived post load - no clean fit
//                for the generic field iteration.

nlohmann::json save(const Name& n)          { return saveReflected(n); }
void load(const nlohmann::json& j, Name& n) { loadReflected(j, n); }

nlohmann::json save(const Transform& t)          { return saveReflected(t); }
void load(const nlohmann::json& j, Transform& t) { loadReflected(j, t); }

nlohmann::json save(const Camera& c)          { return saveReflected(c); }
void load(const nlohmann::json& j, Camera& c) { loadReflected(j, c); }

nlohmann::json save(const Light& l)          { return saveReflected(l); }
void load(const nlohmann::json& j, Light& l) { loadReflected(j, l); }

nlohmann::json save(const Rigidbody& rb)          { return saveReflected(rb); }
void load(const nlohmann::json& j, Rigidbody& rb) { loadReflected(j, rb); }

nlohmann::json save(const Collider& c) {
    nlohmann::json j = saveReflected(c);   // isTrigger
    nlohmann::json arr = nlohmann::json::array();
    for (const ColliderBox& b : c.parts) {
        arr.push_back({
            {"center", vec3ToJson(b.center)},
            {"half",   vec3ToJson(b.halfExtents)},
        });
    }
    j["parts"] = std::move(arr);
    return j;
}
void load(const nlohmann::json& j, Collider& c) {
    loadReflected(j, c);   // isTrigger

    auto it = j.find("parts");
    if (it == j.end() || !it->is_array() || it->empty()) return;  // keep the default unit box

    c.parts.clear();
    c.parts.reserve(it->size());
    for (const auto& e : *it) {
        // at() preserves the throw-on-missing-key behavior (caught by the scene
        // loader's guard); jsonToVec3 adds bounds-checked, logged array reads.
        ColliderBox b;
        b.center      = jsonToVec3(e.at("center"));
        b.halfExtents = jsonToVec3(e.at("half"));
        c.parts.push_back(b);
    }
}

namespace {
// Resolve a saved asset name to a live handle, warning (and leaving the slot
// empty) when it isn't in the asset graph - e.g. a dependency not loaded yet.
template<typename Asset>
Handle<Asset> resolveAssetRef(const ResourceManager& r, const std::string& name, const char* what) {
    if (name.empty()) return {};
    Handle<Asset> h = r.findByName<Asset>(name);
    if (!h) LOG_WARNING("SceneLoad: %s asset '%s' not found - Mesh component left unresolved", what, name.c_str());
    return h;
}
} // namespace

nlohmann::json save(const Mesh& m, const ResourceManager& resources) {
    return {
        {"mesh",        m.mesh     ? resources.get(m.mesh).name     : std::string{}},
        {"material",    m.material ? resources.get(m.material).name : std::string{}},
        {"visible",     m.visible},
        {"castShadows", m.castShadows},
    };
}
void load(const nlohmann::json& j, Mesh& m, const ResourceManager& resources) {
    m.mesh        = resolveAssetRef<MeshAsset>    (resources, j.value("mesh",     std::string{}), "mesh");
    m.material    = resolveAssetRef<MaterialAsset>(resources, j.value("material", std::string{}), "material");
    m.visible     = j.value("visible",     m.visible);
    m.castShadows = j.value("castShadows", m.castShadows);
}

nlohmann::json save(const Hierarchy& h) {
    return nlohmann::json{{"parent", h.parent.index}};
}
uint32_t loadParentIndex(const nlohmann::json& j) {
    return j.value("parent", std::numeric_limits<uint32_t>::max());
}

namespace {

template<typename T, typename ValueWriter>
nlohmann::json saveTrack(const AnimationTrack<T>& track, ValueWriter writeValue) {
    nlohmann::json keyframes = nlohmann::json::array();
    const auto& times  = track.getTimes();
    const auto& values = track.getValues();
    for (size_t i = 0; i < times.size(); ++i) {
        keyframes.push_back({{"t", times[i]}, {"v", writeValue(values[i])}});
    }
    return {
        {"easing",    Easing::nameOf(track.getEasing())},
        {"keyframes", std::move(keyframes)},
    };
}

template<typename T, typename ValueReader>
void loadTrack(const nlohmann::json& j, AnimationTrack<T>& track, ValueReader readValue) {
    track.clear();
    if (j.contains("easing")) {
        track.setEasing(Easing::byName(j["easing"].get<std::string>().c_str()));
    }
    if (j.contains("keyframes") && j["keyframes"].is_array()) {
        for (const auto& kf : j["keyframes"]) {
            track.addKeyframe(kf.value("t", 0.0f), readValue(kf["v"]));
        }
    }
}

} // namespace

nlohmann::json save(const Animation& a) {
    return {
        {"position", saveTrack(a.positionTrack, [](const glm::vec3& v) { return vec3ToJson(v); })},
        {"rotation", saveTrack(a.rotationTrack, [](const glm::quat& q) { return quatToJson(q); })},
        {"scale",    saveTrack(a.scaleTrack,    [](const glm::vec3& v) { return vec3ToJson(v); })},
        {"time",     a.time},
        {"length",   a.length},
        {"speed",    a.speed},
        {"playing",  a.playing},
        {"looping",  a.looping},
    };
}

void load(const nlohmann::json& j, Animation& a) {
    if (j.contains("position")) loadTrack(j["position"], a.positionTrack, [](const nlohmann::json& v) { return jsonToVec3(v); });
    if (j.contains("rotation")) loadTrack(j["rotation"], a.rotationTrack, [](const nlohmann::json& v) { return jsonToQuat(v); });
    if (j.contains("scale"))    loadTrack(j["scale"],    a.scaleTrack,    [](const nlohmann::json& v) { return jsonToVec3(v); });
    a.time    = j.value("time",    a.time);
    a.length  = j.value("length",  a.length);
    a.speed   = j.value("speed",   a.speed);
    a.playing = j.value("playing", a.playing);
    a.looping = j.value("looping", a.looping);
    a.updateDuration();
}

namespace {

/**
 * @brief Writes each visited reflected field into a JSON object (read-only on the
 * behavior - see the const_cast note in save()).
 */
class BehaviorJsonWriter : public BehaviorFieldVisitor {
    public:
        explicit BehaviorJsonWriter(nlohmann::json& out) : m_out(out) {}

        void field(const char* name, float& v) override { m_out[name] = v; }
        void field(const char* name, int& v)   override { m_out[name] = v; }
        void field(const char* name, bool& v)  override { m_out[name] = v; }
        void field(const char* name, glm::vec3& v) override { m_out[name] = vec3ToJson(v); }

    private:
        nlohmann::json& m_out;
};

/**
 * @brief Reads each visited reflected field from a JSON object, keeping the field's
 * current value when the key is missing or malformed.
 */
class BehaviorJsonReader : public BehaviorFieldVisitor {
    public:
        explicit BehaviorJsonReader(const nlohmann::json& in) : m_in(in) {}

        void field(const char* name, float& v) override { if (m_in.contains(name)) v = m_in[name].get<float>(); }
        void field(const char* name, int& v)   override { if (m_in.contains(name)) v = m_in[name].get<int>(); }
        void field(const char* name, bool& v)  override { if (m_in.contains(name)) v = m_in[name].get<bool>(); }
        void field(const char* name, glm::vec3& v) override {
            // jsonToVec3 validates the array shape and falls back to the current
            // value (passed as the fallback) on a missing/malformed node.
            if (m_in.contains(name)) v = jsonToVec3(m_in[name], v);
        }

    private:
        const nlohmann::json& m_in;
};

} // namespace

nlohmann::json save(const ScriptComponent& sc) {
    nlohmann::json behaviors = nlohmann::json::array();
    for (const auto& behavior : sc.behaviors) {
        if (!behavior) continue;
        nlohmann::json props = nlohmann::json::object();
        BehaviorJsonWriter writer(props);
        // visitFields is non-const (shared with the editor/loader, which mutate);
        // the writer only reads field values, so this const_cast is safe.
        const_cast<Behavior&>(*behavior).visitFields(writer);
        behaviors.push_back({{"type", behavior->typeName()}, {"properties", std::move(props)}});
    }
    return {{"behaviors", std::move(behaviors)}};
}

void load(const nlohmann::json& j, ScriptComponent& sc) {
    sc.behaviors.clear();
    if (!j.contains("behaviors") || !j["behaviors"].is_array()) return;
    for (const auto& entry : j["behaviors"]) {
        const std::string type = entry.value("type", std::string{});
        if (type.empty()) continue;
        auto behavior = BehaviorRegistry::get().create(type);
        if (!behavior) continue;
        if (entry.contains("properties") && entry["properties"].is_object()) {
            BehaviorJsonReader reader(entry["properties"]);
            behavior->visitFields(reader);
        }
        sc.behaviors.push_back(std::move(behavior));
    }
}


} // namespace Engine::ComponentSerializer
