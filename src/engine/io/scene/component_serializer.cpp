#define VKM_LOG_CATEGORY "IO"

#include "io/scene/component_serializer.h"

#include <cstddef>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "logger.h"

#include "ecs/component/render/camera.h"
#include "ecs/environment.h"
#include "ecs/component/core/transform.h"
#include "io/json_vec.h"
#include "core/reflect.h"
#include "resource/resource_manager.h"
#include "system/script/behavior.h"
#include "system/script/behavior_field_visitor.h"
#include "system/script/behavior_registry.h"

namespace Vkm::Engine::ComponentSerializer {

namespace {

using ::Vkm::Engine::detail::vec2ToJson;
using ::Vkm::Engine::detail::vec3ToJson;
using ::Vkm::Engine::detail::vec4ToJson;
using ::Vkm::Engine::detail::quatToJson;
using ::Vkm::Engine::detail::jsonToVec2;
using ::Vkm::Engine::detail::jsonToVec3;
using ::Vkm::Engine::detail::jsonToVec4;
using ::Vkm::Engine::detail::jsonToQuat;

// toJson / fromJson overload set. The reflection-driven save/load templates
// (saveReflected / loadReflected) iterate a type's fields and forward each
// to these helpers; adding a new field type means adding a new pair here.
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

// Reflection driver. Phase-1 unqualified lookup at this definition site picks
// up every overload declared above - no ADL needed, they and the templates all
// live in this anon namespace - so a new field type only needs a matching pair.

template<typename T>
nlohmann::json saveReflected(const T& obj);
template<typename T>
void loadReflected(const nlohmann::json& j, T& obj);

// Nested reflected structs: a settings block that groups its own fields writes
// as a nested object rather than flattening its names into the parent. A struct
// that has been through VKM_REFLECT is not a leaf, so recurse instead of looking
// for a toJson that cannot exist. Declared after the driver so the recursion
// resolves.
// SFINAE goes in the return type, not a defaulted template parameter: a default
// argument is not part of the signature, so the enum pair above and this one
// would be redefinitions of each other.
template<typename T>
inline std::enable_if_t<Reflect::IS_REFLECTED<T>, nlohmann::json> toJson(const T& v) {
    return saveReflected(v);
}
template<typename T>
inline std::enable_if_t<Reflect::IS_REFLECTED<T>> fromJson(const nlohmann::json& j, T& v) {
    loadReflected(j, v);
}

template<typename T>
nlohmann::json saveReflected(const T& obj) {
    nlohmann::json out = nlohmann::json::object();
    ::Vkm::Engine::Reflect::forEachField(obj, [&](std::string_view name, const auto& val) {
        out[std::string(name)] = toJson(val);
    });
    return out;
}

template<typename T>
void loadReflected(const nlohmann::json& j, T& obj) {
    ::Vkm::Engine::Reflect::forEachField(obj, [&](std::string_view name, auto& val) {
        auto it = j.find(std::string(name));
        if (it != j.end()) fromJson(*it, val);
    });
}

} // namespace

// Component save / load. Reflectable components are one-line passthroughs
// into the reflection driver. The exceptions are intentional:
//   - Mesh:      ResourceManager handle lookup (cross-asset reference).
//   - Animator:  the same, plus a persisted surface narrower than the struct -
//                blend state is transient by design.
//   - Hierarchy: parent stored as raw scene-table index, resolved by
//                SceneSerializer after the entity table is loaded.
//   - Animation: AnimationTrack<T> keeps its keyframes private and must go
//                through explicit accessors - no clean fit for field iteration.

nlohmann::json save(const Environment& env) {
    return saveReflected(env);
}

void load(const nlohmann::json& j, Environment& env) {
    loadReflected(j, env);
}

nlohmann::json save(const PhysicsSettings& p)          { return saveReflected(p); }
void load(const nlohmann::json& j, PhysicsSettings& p) { loadReflected(j, p); }

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

nlohmann::json save(const CharacterController& cc)          { return saveReflected(cc); }
void load(const nlohmann::json& j, CharacterController& cc) { loadReflected(j, cc); }

nlohmann::json save(const Collider& c) {
    nlohmann::json j = saveReflected(c);   // isTrigger + enabled
    nlohmann::json arr = nlohmann::json::array();
    for (const ColliderPart& p : c.parts) {
        // Every shape's fields are written whatever the tag says, so switching a
        // part to a capsule in the inspector and back does not quietly forget
        // the half-extents it was authored with.
        arr.push_back({
            {"shape",      Reflect::enumName(p.shape)},
            {"center",     vec3ToJson(p.center)},
            {"half",       vec3ToJson(p.halfExtents)},
            {"radius",     p.radius},
            {"halfHeight", p.halfHeight},
        });
    }
    j["parts"] = std::move(arr);
    return j;
}
void load(const nlohmann::json& j, Collider& c) {
    loadReflected(j, c);   // isTrigger + enabled

    auto it = j.find("parts");
    if (it == j.end() || !it->is_array() || it->empty()) return;  // keep the default unit box

    c.parts.clear();
    c.parts.reserve(it->size());
    for (const auto& e : *it) {
        // at() preserves the throw-on-missing-key behavior (caught by the scene
        // loader's guard); jsonToVec3 adds bounds-checked, logged array reads.
        ColliderPart p;
        p.shape       = Reflect::enumFromName<ColliderShape>(e.at("shape").get<std::string>());
        p.center      = jsonToVec3(e.at("center"));
        p.halfExtents = jsonToVec3(e.at("half"));
        p.radius      = e.at("radius").get<float>();
        p.halfHeight  = e.at("halfHeight").get<float>();
        c.parts.push_back(p);
    }
}

namespace {
// Resolve a saved asset name to a live handle, warning (and leaving the slot
// empty) when it isn't in the asset graph - e.g. a dependency not loaded yet.
template<typename Asset>
Handle<Asset> resolveAssetRef(const ResourceManager& r, const std::string& name, const char* what) {
    if (name.empty()) return {};
    Handle<Asset> h = r.findByName<Asset>(name);
    if (!h) LOG_WARNING("SceneLoad: %s asset '%s' not found - reference left unresolved", what, name.c_str());
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

nlohmann::json save(const Animator& a, const ResourceManager& resources) {
    return {
        {"skeleton", a.skeleton ? resources.get(a.skeleton).name : std::string{}},
        {"clip",     a.clip     ? resources.get(a.clip).name     : std::string{}},
        {"time",     a.time},
        {"speed",    a.speed},
        {"playing",  a.playing},
        {"looping",  a.looping},
    };
}
void load(const nlohmann::json& j, Animator& a, const ResourceManager& resources) {
    a.skeleton = resolveAssetRef<SkeletonAsset>     (resources, j.value("skeleton", std::string{}), "skeleton");
    a.clip     = resolveAssetRef<AnimationClipAsset>(resources, j.value("clip",     std::string{}), "clip");
    a.time     = j.value("time",    a.time);
    a.speed    = j.value("speed",   a.speed);
    a.playing  = j.value("playing", a.playing);
    a.looping  = j.value("looping", a.looping);
}

nlohmann::json save(const LOD& l, const ResourceManager& resources) {
    nlohmann::json levels = nlohmann::json::array();
    for (const LODLevel& level : l.levels) {
        if (!level.mesh) continue;   // an unresolved level would load as a hole in the ramp
        levels.push_back({
            {"mesh",        resources.get(level.mesh).name},
            {"maxDistance", level.maxDistance},
        });
    }
    return {{"levels", levels}, {"bias", l.bias}};
}
void load(const nlohmann::json& j, LOD& l, const ResourceManager& resources) {
    l.bias = j.value("bias", l.bias);
    if (!j.contains("levels")) return;
    for (const auto& entry : j["levels"]) {
        MeshHandle mesh = resolveAssetRef<MeshAsset>(
            resources, entry.value("mesh", std::string{}), "LOD mesh");
        if (!mesh) continue;
        l.levels.push_back({mesh, entry.value("maxDistance", 0.0f)});
    }
}

nlohmann::json save(const Decal& d, const ResourceManager& resources) {
    return {
        {"material",  d.material ? resources.get(d.material).name : std::string{}},
        {"angleFade", d.angleFade},
        {"opacity",   d.opacity},
    };
}
void load(const nlohmann::json& j, Decal& d, const ResourceManager& resources) {
    d.material  = resolveAssetRef<MaterialAsset>(resources, j.value("material", std::string{}), "material");
    d.angleFade = j.value("angleFade", d.angleFade);
    d.opacity   = j.value("opacity",   d.opacity);
}

nlohmann::json save(const ParticleEmitter& e)          { return saveReflected(e); }
void load(const nlohmann::json& j, ParticleEmitter& e) { loadReflected(j, e); }

nlohmann::json save(const ReflectionProbe& p)          { return saveReflected(p); }
void load(const nlohmann::json& j, ReflectionProbe& p) { loadReflected(j, p); }

nlohmann::json save(const IrradianceVolume& v)          { return saveReflected(v); }
void load(const nlohmann::json& j, IrradianceVolume& v) { loadReflected(j, v); }

nlohmann::json save(const UICanvas& c)          { return saveReflected(c); }
void load(const nlohmann::json& j, UICanvas& c) { loadReflected(j, c); }

nlohmann::json save(const UIElement& e)          { return saveReflected(e); }
void load(const nlohmann::json& j, UIElement& e) { loadReflected(j, e); }

nlohmann::json save(const UIImage& i)          { return saveReflected(i); }
void load(const nlohmann::json& j, UIImage& i) { loadReflected(j, i); }

nlohmann::json save(const UIText& t)          { return saveReflected(t); }
void load(const nlohmann::json& j, UIText& t) { loadReflected(j, t); }

nlohmann::json save(const UIButton& b)          { return saveReflected(b); }
void load(const nlohmann::json& j, UIButton& b) { loadReflected(j, b); }

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
            // at(), not operator[]: const operator[] on a missing key only
            // asserts, which is nothing in release, and then reads the map's end
            // node. The throw is caught by the scene loader's guard.
            track.addKeyframe(kf.value("t", 0.0f), readValue(kf.at("v")));
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
}

namespace {

/**
 * @brief Writes each visited reflected field into a JSON object (read-only on the
 * behavior - see the const_cast note in save()).
 *
 * A nested reflected struct becomes a JSON sub-object: beginStruct pushes it and
 * endStruct pops, so `cur()` is always the object the current fields write into.
 */
class BehaviorJsonWriter : public BehaviorFieldVisitor {
    public:
        explicit BehaviorJsonWriter(nlohmann::json& out) { m_scopes.push_back(&out); }

        void field(const char* name, float& v) override { cur()[name] = v; }
        void field(const char* name, int& v)   override { cur()[name] = v; }
        void field(const char* name, bool& v)  override { cur()[name] = v; }
        void field(const char* name, glm::vec3& v) override { cur()[name] = vec3ToJson(v); }

        void enumField(const char* name, int& index, const char* const* names, std::size_t count) override {
            // By name, not the raw index: reordering enum values without renaming
            // then leaves existing scenes valid (matches Reflect::enumName).
            if (index >= 0 && static_cast<std::size_t>(index) < count) cur()[name] = names[index];
        }

        bool beginStruct(const char* name) override {
            m_scopes.push_back(&(cur()[name] = nlohmann::json::object()));
            return true;
        }
        void endStruct() override { m_scopes.pop_back(); }

    private:
        // std::map-backed json: a reference to a nested value stays valid as
        // siblings are inserted, so holding these pointers across the walk is safe.
        nlohmann::json& cur() { return *m_scopes.back(); }
        std::vector<nlohmann::json*> m_scopes;
};

/**
 * @brief Reads each visited reflected field from a JSON object, keeping the field's
 * current value when the key is missing or malformed.
 *
 * A nested struct descends into its sub-object; a missing/mistyped sub-object is
 * skipped (beginStruct returns false), leaving that struct's fields at their
 * constructed defaults - the same keep-current-value rule the leaves follow.
 */
class BehaviorJsonReader : public BehaviorFieldVisitor {
    public:
        explicit BehaviorJsonReader(const nlohmann::json& in) { m_scopes.push_back(&in); }

        void field(const char* name, float& v) override { if (cur().contains(name)) v = cur()[name].get<float>(); }
        void field(const char* name, int& v)   override { if (cur().contains(name)) v = cur()[name].get<int>(); }
        void field(const char* name, bool& v)  override { if (cur().contains(name)) v = cur()[name].get<bool>(); }
        void field(const char* name, glm::vec3& v) override {
            // The current value goes in as the fallback: a malformed node keeps it.
            if (cur().contains(name)) v = jsonToVec3(cur()[name], v);
        }

        void enumField(const char* name, int& index, const char* const* names, std::size_t count) override {
            if (!cur().contains(name) || !cur()[name].is_string()) return;   // keep current
            const std::string picked = cur()[name].get<std::string>();
            for (std::size_t i = 0; i < count; ++i) {
                if (picked == names[i]) { index = static_cast<int>(i); return; }
            }
            // Unknown name (a removed/renamed value): keep the current value.
        }

        bool beginStruct(const char* name) override {
            if (!cur().contains(name) || !cur()[name].is_object()) return false;   // keep current
            m_scopes.push_back(&cur()[name]);
            return true;
        }
        void endStruct() override { m_scopes.pop_back(); }

    private:
        const nlohmann::json& cur() { return *m_scopes.back(); }
        std::vector<const nlohmann::json*> m_scopes;
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

} // namespace Vkm::Engine::ComponentSerializer
