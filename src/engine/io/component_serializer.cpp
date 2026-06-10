#define VKM_LOG_CATEGORY "IO"

#include "io/component_serializer.h"

#include <cstddef>
#include <cstring>
#include <limits>
#include <string>

#include "logger.h"

#include "ecs/component/camera.h"
#include "ecs/component/transform.h"
#include "io/reflect.h"
#include "resource/resource_manager.h"

namespace Engine::ComponentSerializer {

namespace {

[[maybe_unused]] nlohmann::json vec2ToJson(const glm::vec2& v) { return {v.x, v.y}; }
nlohmann::json vec3ToJson(const glm::vec3& v) { return {v.x, v.y, v.z}; }
[[maybe_unused]] nlohmann::json vec4ToJson(const glm::vec4& v) { return {v.x, v.y, v.z, v.w}; }
nlohmann::json quatToJson(const glm::quat& q) { return {q.w, q.x, q.y, q.z}; }

[[maybe_unused]] glm::vec2 vec2FromJson(const nlohmann::json& j) { return {j[0], j[1]}; }
glm::vec3 vec3FromJson(const nlohmann::json& j) { return {j[0], j[1], j[2]}; }
[[maybe_unused]] glm::vec4 vec4FromJson(const nlohmann::json& j) { return {j[0], j[1], j[2], j[3]}; }
glm::quat quatFromJson(const nlohmann::json& j) {
    return glm::quat(static_cast<float>(j[0]), static_cast<float>(j[1]),
                     static_cast<float>(j[2]), static_cast<float>(j[3]));
}

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
inline void fromJson(const nlohmann::json& j, glm::vec2& v) { if (j.is_array() && j.size() >= 2) v = vec2FromJson(j); }
inline void fromJson(const nlohmann::json& j, glm::vec3& v) { if (j.is_array() && j.size() >= 3) v = vec3FromJson(j); }
inline void fromJson(const nlohmann::json& j, glm::vec4& v) { if (j.is_array() && j.size() >= 4) v = vec4FromJson(j); }
inline void fromJson(const nlohmann::json& j, glm::quat& q) { if (j.is_array() && j.size() >= 4) q = quatFromJson(j); }

// Fixed-size character buffers (e.g. Name::value is char[64]).
template<std::size_t N>
inline nlohmann::json toJson(const char (&v)[N]) { return std::string(v); }
template<std::size_t N>
inline void fromJson(const nlohmann::json& j, char (&v)[N]) {
    const std::string s = j.get<std::string>();
    std::strncpy(v, s.c_str(), N - 1);
    v[N - 1] = '\0';
}

// Enums.
inline nlohmann::json toJson(ProjectionType v) {
    return Reflect::enumName(v, PROJECTION_TYPE_NAMES);
}
inline void fromJson(const nlohmann::json& j, ProjectionType& v) {
    v = Reflect::enumFromName<ProjectionType>(j.get<std::string>(), PROJECTION_TYPE_NAMES);
}

inline nlohmann::json toJson(LightType t) {
    return Reflect::enumName(t, LIGHT_TYPE_NAMES);
}
inline void fromJson(const nlohmann::json& j, LightType& v) {
    v = Reflect::enumFromName<LightType>(j.get<std::string>(), LIGHT_TYPE_NAMES);
}

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
//   - Animation: AnimationTrack<T> exposes only addKeyframe / getTimes
//                (private storage) and updateDuration() must be re-derived
//                post load - no clean fit for the generic field iteration.

nlohmann::json save(const Name& n)         { return saveReflected(n); }
void load(const nlohmann::json& j, Name& n) { loadReflected(j, n); }

nlohmann::json save(const Transform& t)         { return saveReflected(t); }
void load(const nlohmann::json& j, Transform& t) { loadReflected(j, t); }

nlohmann::json save(const Camera& c)         { return saveReflected(c); }
void load(const nlohmann::json& j, Camera& c) { loadReflected(j, c); }

nlohmann::json save(const Light& l)         { return saveReflected(l); }
void load(const nlohmann::json& j, Light& l) { loadReflected(j, l); }

nlohmann::json save(const Rigidbody& rb) { return saveReflected(rb); }
void load(const nlohmann::json& j, Rigidbody& rb) { loadReflected(j, rb); }

nlohmann::json save(const Collider& c) {
    nlohmann::json j = saveReflected(c);   // isTrigger
    nlohmann::json arr = nlohmann::json::array();
    for (const ColliderBox& b : c.parts) {
        arr.push_back({
            {"center", {b.center.x, b.center.y, b.center.z}},
            {"half",   {b.halfExtents.x, b.halfExtents.y, b.halfExtents.z}},
        });
    }
    j["parts"] = std::move(arr);
    return j;
}
void load(const nlohmann::json& j, Collider& c) {
    loadReflected(j, c);   // isTrigger
    c.parts.clear();

    if (auto it = j.find("parts"); it != j.end() && it->is_array() && !it->empty()) {
        c.parts.reserve(it->size());
        for (const auto& e : *it) {
            const auto& ctr = e.at("center");
            const auto& hf  = e.at("half");
            ColliderBox b;
            b.center      = {ctr[0].get<float>(), ctr[1].get<float>(), ctr[2].get<float>()};
            b.halfExtents = {hf[0].get<float>(),  hf[1].get<float>(),  hf[2].get<float>()};
            c.parts.push_back(b);
        }
        return;
    }

    // Legacy migration: scenes saved before colliders became box lists carried a
    // primitive "shape" + radius/halfExtents/plane. Collapse each to one box so
    // they still load (Sphere -> enclosing box, Plane -> a large thin slab).
    const std::string shape = j.value("shape", std::string("Box"));
    ColliderBox box;
    if (shape == "Sphere") {
        box.halfExtents = glm::vec3(j.value("radius", 0.5f));
    } else if (shape == "Plane") {
        box.center      = glm::vec3(0.0f, j.value("planeOffset", 0.0f), 0.0f);
        box.halfExtents = glm::vec3(1000.0f, 0.01f, 1000.0f);
    } else if (auto he = j.find("halfExtents"); he != j.end() && he->is_array() && he->size() == 3) {
        box.halfExtents = {(*he)[0].get<float>(), (*he)[1].get<float>(), (*he)[2].get<float>()};
    }
    c.parts = { box };
}

nlohmann::json save(const PhysicsWorld& w) { return saveReflected(w); }
void load(const nlohmann::json& j, PhysicsWorld& w) { loadReflected(j, w); }

nlohmann::json save(const Mesh& m, const ResourceManager& resources) {
    const AssetId meshId     = m.mesh     ? resources.get(m.mesh).assetId     : AssetId{};
    const AssetId materialId = m.material ? resources.get(m.material).assetId : AssetId{};
    return {
        {"mesh",        meshId.toString()},
        {"material",    materialId.toString()},
        {"visible",     m.visible},
        {"castShadows", m.castShadows},
    };
}
void load(const nlohmann::json& j, Mesh& m, const ResourceManager& resources) {
    const AssetId meshId     = AssetId::fromString(j.value("mesh",     std::string{}));
    const AssetId materialId = AssetId::fromString(j.value("material", std::string{}));

    if (meshId) {
        m.mesh = resources.findById<MeshAsset>(meshId);
        if (!m.mesh) {
            LOG_WARNING("SceneLoad: mesh asset %s not found - Mesh component left unresolved",
                        meshId.toString().c_str());
        }
    }
    if (materialId) {
        m.material = resources.findById<MaterialAsset>(materialId);
        if (!m.material) {
            LOG_WARNING("SceneLoad: material asset %s not found - Mesh component left unresolved",
                        materialId.toString().c_str());
        }
    }

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
    if (j.contains("position")) loadTrack(j["position"], a.positionTrack, vec3FromJson);
    if (j.contains("rotation")) loadTrack(j["rotation"], a.rotationTrack, quatFromJson);
    if (j.contains("scale"))    loadTrack(j["scale"],    a.scaleTrack,    vec3FromJson);
    a.time    = j.value("time",    a.time);
    a.length  = j.value("length",  a.length);
    a.speed   = j.value("speed",   a.speed);
    a.playing = j.value("playing", a.playing);
    a.looping = j.value("looping", a.looping);
    a.updateDuration();
}


} // namespace Engine::ComponentSerializer

// Reflection markups. Listed at file scope so the VKM_REFLECT_BEGIN macro
// can re-open Engine::Reflect and specialise Traits<T> there without
// fighting the surrounding namespace. Each entry lists the JSON-persisted
// fields; fields omitted here are NOT serialised (e.g. ReflectionProbe::
// bakeVersion, see save(ReflectionProbe) above).

VKM_REFLECT_BEGIN(Engine::Name)
    VKM_F(value)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Engine::Transform)
    VKM_F(position),
    VKM_F(rotation),
    VKM_F(scale)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Engine::Camera)
    VKM_F(projection),
    VKM_F(fovY),
    VKM_F(orthoHeight),
    VKM_F(aspect),
    VKM_F(zNear),
    VKM_F(zFar),
    VKM_F(exposure),
    VKM_F(active)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Engine::Light)
    VKM_F(type),
    VKM_F(color),
    VKM_F(intensity),
    VKM_F(radius),
    VKM_F(innerConeAngle),
    VKM_F(outerConeAngle),
    VKM_F(areaWidth),
    VKM_F(areaHeight),
    VKM_F(areaRadius),
    VKM_F(twoSided),
    VKM_F(castShadows),
    VKM_F(shadowBias),
    VKM_F(shadowExtent),
    VKM_F(enabled)
VKM_REFLECT_END()

// inverseMass / invInertiaLocal are derived from mass + Collider on load;
// sleeping / sleepTimer are runtime-only. All are intentionally absent.
VKM_REFLECT_BEGIN(Engine::Rigidbody)
    VKM_F(linearVelocity),
    VKM_F(angularVelocity),
    VKM_F(mass),
    VKM_F(linearDamping),
    VKM_F(angularDamping),
    VKM_F(restitution),
    VKM_F(friction),
    VKM_F(gravityScale),
    VKM_F(isKinematic),
    VKM_F(isStatic)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Engine::PhysicsWorld)
    VKM_F(gravity),
    VKM_F(solverIterations)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Engine::Collider)
    VKM_F(isTrigger)
VKM_REFLECT_END()

