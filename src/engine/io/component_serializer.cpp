#define VKM_LOG_CATEGORY "IO"

#include "io/component_serializer.h"

#include <cstddef>
#include <cstring>
#include <limits>
#include <string>

#include "logger.h"

#include "ecs/component/camera.h"
#include "ecs/component/reflection_probe.h"
#include "ecs/component/transform.h"
#include "io/reflect.h"
#include "resource/resource_manager.h"
#include "system/render/environment_config.h"   // EnvironmentConfig + every sub-config

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

inline nlohmann::json toJson(ColliderShape s) {
    return Reflect::enumName(s, COLLIDER_SHAPE_NAMES);
}
inline void fromJson(const nlohmann::json& j, ColliderShape& v) {
    v = Reflect::enumFromName<ColliderShape>(j.get<std::string>(), COLLIDER_SHAPE_NAMES);
}

inline nlohmann::json toJson(RenderMode m) { return static_cast<int>(m); }
inline void fromJson(const nlohmann::json& j, RenderMode& m) {
    m = static_cast<RenderMode>(j.get<int>());
}

// Forward declarations for nested-config bridge overloads. These have to
// be VISIBLE at the saveReflected / loadReflected definition site - the
// templates do unqualified lookup of toJson / fromJson at first phase, and
// ADL at the point of instantiation can only find names in the argument
// type's associated namespaces (i.e. ::Engine, not this anon namespace).
// So every overload that participates in reflected iteration must be
// declared above the templates.

inline nlohmann::json toJson(const AmbientConfig& c);
inline void fromJson(const nlohmann::json& j, AmbientConfig& c);
inline nlohmann::json toJson(const IBLConfig& c);
inline void fromJson(const nlohmann::json& j, IBLConfig& c);
inline nlohmann::json toJson(const SkyboxConfig& c);
inline void fromJson(const nlohmann::json& j, SkyboxConfig& c);
inline nlohmann::json toJson(const AOConfig& c);
inline void fromJson(const nlohmann::json& j, AOConfig& c);
inline nlohmann::json toJson(const SSRConfig& c);
inline void fromJson(const nlohmann::json& j, SSRConfig& c);
inline nlohmann::json toJson(const TAAConfig& c);
inline void fromJson(const nlohmann::json& j, TAAConfig& c);
inline nlohmann::json toJson(const DofConfig& c);
inline void fromJson(const nlohmann::json& j, DofConfig& c);
inline nlohmann::json toJson(const MotionBlurConfig& c);
inline void fromJson(const nlohmann::json& j, MotionBlurConfig& c);
inline nlohmann::json toJson(const StarburstConfig& c);
inline void fromJson(const nlohmann::json& j, StarburstConfig& c);
inline nlohmann::json toJson(const LensFlareConfig& c);
inline void fromJson(const nlohmann::json& j, LensFlareConfig& c);
inline nlohmann::json toJson(const LensDirtConfig& c);
inline void fromJson(const nlohmann::json& j, LensDirtConfig& c);
inline nlohmann::json toJson(const BloomConfig& c);
inline void fromJson(const nlohmann::json& j, BloomConfig& c);
inline nlohmann::json toJson(const ShadowConfig& c);
inline void fromJson(const nlohmann::json& j, ShadowConfig& c);
inline nlohmann::json toJson(const TransparencyConfig& c);
inline void fromJson(const nlohmann::json& j, TransparencyConfig& c);
inline nlohmann::json toJson(const OcclusionConfig& c);
inline void fromJson(const nlohmann::json& j, OcclusionConfig& c);
inline nlohmann::json toJson(const ExposureConfig& c);
inline void fromJson(const nlohmann::json& j, ExposureConfig& c);
inline nlohmann::json toJson(const ColorGradeConfig& c);
inline void fromJson(const nlohmann::json& j, ColorGradeConfig& c);
inline nlohmann::json toJson(const GridConfig& c);
inline void fromJson(const nlohmann::json& j, GridConfig& c);
inline nlohmann::json toJson(const AABBDebugConfig& c);
inline void fromJson(const nlohmann::json& j, AABBDebugConfig& c);
inline nlohmann::json toJson(const SelectionOutlineConfig& c);
inline void fromJson(const nlohmann::json& j, SelectionOutlineConfig& c);
inline nlohmann::json toJson(const MSAAConfig& c);
inline void fromJson(const nlohmann::json& j, MSAAConfig& c);

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

// Bridge definitions - each nested config is a one-line re-entry into the
// reflection driver, so a parent struct can embed it as a reflected field
// with no per-effect plumbing. ExposureConfig and EnvironmentConfig
// override fromJson because they carry small back-compat fixups around
// fields renamed in earlier refactors.

inline nlohmann::json toJson(const AmbientConfig& c)             { return saveReflected(c); }
inline void fromJson(const nlohmann::json& j, AmbientConfig& c)             { loadReflected(j, c); }

inline nlohmann::json toJson(const IBLConfig& c)                 { return saveReflected(c); }
inline void fromJson(const nlohmann::json& j, IBLConfig& c)                 { loadReflected(j, c); }

inline nlohmann::json toJson(const SkyboxConfig& c)              { return saveReflected(c); }
inline void fromJson(const nlohmann::json& j, SkyboxConfig& c)              { loadReflected(j, c); }

inline nlohmann::json toJson(const AOConfig& c)                  { return saveReflected(c); }
inline void fromJson(const nlohmann::json& j, AOConfig& c)                  { loadReflected(j, c); }

inline nlohmann::json toJson(const SSRConfig& c)                 { return saveReflected(c); }
inline void fromJson(const nlohmann::json& j, SSRConfig& c)                 { loadReflected(j, c); }

inline nlohmann::json toJson(const TAAConfig& c)                 { return saveReflected(c); }
inline void fromJson(const nlohmann::json& j, TAAConfig& c)                 { loadReflected(j, c); }

inline nlohmann::json toJson(const DofConfig& c)                 { return saveReflected(c); }
inline void fromJson(const nlohmann::json& j, DofConfig& c)                 { loadReflected(j, c); }

inline nlohmann::json toJson(const MotionBlurConfig& c)          { return saveReflected(c); }
inline void fromJson(const nlohmann::json& j, MotionBlurConfig& c)          { loadReflected(j, c); }

inline nlohmann::json toJson(const StarburstConfig& c)           { return saveReflected(c); }
inline void fromJson(const nlohmann::json& j, StarburstConfig& c)           { loadReflected(j, c); }

inline nlohmann::json toJson(const LensFlareConfig& c)           { return saveReflected(c); }
inline void fromJson(const nlohmann::json& j, LensFlareConfig& c)           { loadReflected(j, c); }

inline nlohmann::json toJson(const LensDirtConfig& c)            { return saveReflected(c); }
inline void fromJson(const nlohmann::json& j, LensDirtConfig& c)            { loadReflected(j, c); }

inline nlohmann::json toJson(const BloomConfig& c)               { return saveReflected(c); }
inline void fromJson(const nlohmann::json& j, BloomConfig& c)               { loadReflected(j, c); }

inline nlohmann::json toJson(const ShadowConfig& c)              { return saveReflected(c); }
inline void fromJson(const nlohmann::json& j, ShadowConfig& c)              { loadReflected(j, c); }

inline nlohmann::json toJson(const TransparencyConfig& c)        { return saveReflected(c); }
inline void fromJson(const nlohmann::json& j, TransparencyConfig& c)        { loadReflected(j, c); }

inline nlohmann::json toJson(const OcclusionConfig& c)           { return saveReflected(c); }
inline void fromJson(const nlohmann::json& j, OcclusionConfig& c)           { loadReflected(j, c); }

inline nlohmann::json toJson(const ExposureConfig& c)            { return saveReflected(c); }
inline void fromJson(const nlohmann::json& j, ExposureConfig& c) {
    loadReflected(j, c);
    // Back-compat: scenes saved before the brighten/darken split used a
    // single "speed" field. Map it to both rates.
    if (j.contains("speed") && !j.contains("speedBrighten") && !j.contains("speedDarken")) {
        c.speedBrighten = j["speed"].get<float>();
        c.speedDarken   = j["speed"].get<float>();
    }
}

inline nlohmann::json toJson(const ColorGradeConfig& c)          { return saveReflected(c); }
inline void fromJson(const nlohmann::json& j, ColorGradeConfig& c)          { loadReflected(j, c); }

inline nlohmann::json toJson(const GridConfig& c)                { return saveReflected(c); }
inline void fromJson(const nlohmann::json& j, GridConfig& c)                { loadReflected(j, c); }

inline nlohmann::json toJson(const AABBDebugConfig& c)           { return saveReflected(c); }
inline void fromJson(const nlohmann::json& j, AABBDebugConfig& c)           { loadReflected(j, c); }

inline nlohmann::json toJson(const SelectionOutlineConfig& c)    { return saveReflected(c); }
inline void fromJson(const nlohmann::json& j, SelectionOutlineConfig& c)    { loadReflected(j, c); }

inline nlohmann::json toJson(const MSAAConfig& c)                { return saveReflected(c); }
inline void fromJson(const nlohmann::json& j, MSAAConfig& c)                { loadReflected(j, c); }

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

nlohmann::json save(const Collider& c) { return saveReflected(c); }
void load(const nlohmann::json& j, Collider& c) { loadReflected(j, c); }

nlohmann::json save(const PhysicsWorld& w) { return saveReflected(w); }
void load(const nlohmann::json& j, PhysicsWorld& w) { loadReflected(j, w); }

// ReflectionProbe::bakeVersion is intentionally absent from the markup so
// the backend re-bakes on load and the counter restarts at 0. That's the
// idiomatic "internal only" pattern: omit the field from VKM_REFLECT.
nlohmann::json save(const ReflectionProbe& p)         { return saveReflected(p); }
void load(const nlohmann::json& j, ReflectionProbe& p) { loadReflected(j, p); }

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

nlohmann::json save(const MeshLOD& lod, const ResourceManager& resources) {
    nlohmann::json levels = nlohmann::json::array();
    for (int i = 0; i < lod.count && i < MeshLOD::MAX_LEVELS; ++i) {
        const AssetId id = lod.levels[i] ? resources.get(lod.levels[i]).assetId : AssetId{};
        levels.push_back({
            {"mesh",         id.toString()},
            {"switchHeight", lod.switchHeights[i]},  // index 0 unused; round-trips for alignment.
        });
    }
    return {
        {"count",  lod.count},
        {"levels", std::move(levels)},
    };
}
void load(const nlohmann::json& j, MeshLOD& lod, const ResourceManager& resources) {
    lod = MeshLOD{};  // reset to a clean single-level default before filling.
    if (j.contains("levels") && j["levels"].is_array()) {
        const auto& levels = j["levels"];
        int n = static_cast<int>(levels.size());
        if (n > MeshLOD::MAX_LEVELS) n = MeshLOD::MAX_LEVELS;
        for (int i = 0; i < n; ++i) {
            const AssetId id = AssetId::fromString(levels[i].value("mesh", std::string{}));
            if (id) {
                lod.levels[i] = resources.findById<MeshAsset>(id);
                if (!lod.levels[i]) {
                    LOG_WARNING("SceneLoad: LOD level %d mesh asset %s not found - left unresolved",
                                i, id.toString().c_str());
                }
            }
            lod.switchHeights[i] = levels[i].value("switchHeight", 0.0f);
        }
    }
    int count = j.value("count", 0);
    if (count < 0)                    count = 0;
    if (count > MeshLOD::MAX_LEVELS)  count = MeshLOD::MAX_LEVELS;
    lod.count = static_cast<uint8_t>(count);
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

// EnvironmentConfig and every sub-config flow through reflection now; the
// only piece that escapes the generic load path is the legacy "wireframe"
// bool from pre-RenderMode scenes.
nlohmann::json save(const EnvironmentConfig& e) { return saveReflected(e); }
void load(const nlohmann::json& j, EnvironmentConfig& e) {
    loadReflected(j, e);
    if (!j.contains("renderMode") && j.value("wireframe", false)) {
        e.renderMode = RenderMode::Wireframe;
    }
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
    VKM_F(shape),
    VKM_F(radius),
    VKM_F(halfExtents),
    VKM_F(planeNormal),
    VKM_F(planeOffset),
    VKM_F(isTrigger)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Engine::ReflectionProbe)
    VKM_F(hdrPath),
    VKM_F(radius),
    VKM_F(falloffRange),
    VKM_F(intensity)
    // bakeVersion is intentionally absent - see save(ReflectionProbe).
VKM_REFLECT_END()

// EnvironmentConfig sub-configs.

VKM_REFLECT_BEGIN(Engine::AmbientConfig)
    VKM_F(color),
    VKM_F(intensity)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Engine::IBLConfig)
    VKM_F(path),
    VKM_F(intensity)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Engine::SkyboxConfig)
    VKM_F(enabled)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Engine::AOConfig)
    VKM_F(enabled),
    VKM_F(radius),
    VKM_F(intensity)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Engine::SSRConfig)
    VKM_F(enabled),
    VKM_F(intensity),
    VKM_F(maxDistance),
    VKM_F(thickness)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Engine::TAAConfig)
    VKM_F(enabled),
    VKM_F(blend)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Engine::DofConfig)
    VKM_F(enabled),
    VKM_F(focusDistance),
    VKM_F(focusRange),
    VKM_F(maxBlur)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Engine::MotionBlurConfig)
    VKM_F(enabled),
    VKM_F(strength)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Engine::StarburstConfig)
    VKM_F(enabled),
    VKM_F(intensity)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Engine::LensFlareConfig)
    VKM_F(enabled),
    VKM_F(intensity),
    VKM_F(threshold),
    VKM_F(ghostCount),
    VKM_F(ghostSpacing),
    VKM_F(haloRadius),
    VKM_F(chromatic),
    VKM_F(starburst)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Engine::LensDirtConfig)
    VKM_F(enabled),
    VKM_F(intensity)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Engine::BloomConfig)
    VKM_F(strength),
    VKM_F(threshold),
    VKM_F(knee)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Engine::ShadowConfig)
    VKM_F(atlasRes2D),
    VKM_F(atlasResCube),
    VKM_F(softness)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Engine::TransparencyConfig)
    VKM_F(useOIT)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Engine::OcclusionConfig)
    VKM_F(useHiZ)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Engine::ExposureConfig)
    VKM_F(autoExposure),
    VKM_F(key),
    VKM_F(speedBrighten),
    VKM_F(speedDarken),
    VKM_F(min),
    VKM_F(max)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Engine::ColorGradeConfig)
    VKM_F(enabled),
    VKM_F(lutPath),
    VKM_F(intensity)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Engine::GridConfig)
    VKM_F(enabled),
    VKM_F(size),
    VKM_F(scale),
    VKM_F(fadeStart),
    VKM_F(fadeEnd)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Engine::AABBDebugConfig)
    VKM_F(enabled),
    VKM_F(color)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Engine::SelectionOutlineConfig)
    VKM_F(enabled),
    VKM_F(color),
    VKM_F(thickness)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Engine::MSAAConfig)
    VKM_F(samples)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Engine::EnvironmentConfig)
    VKM_F(ambient),
    VKM_F(ibl),
    VKM_F(skybox),
    VKM_F(shadow),
    VKM_F(transparency),
    VKM_F(occlusion),
    VKM_F(ao),
    VKM_F(ssr),
    VKM_F(taa),
    VKM_F(dof),
    VKM_F(motionBlur),
    VKM_F(lensFlare),
    VKM_F(lensDirt),
    VKM_F(bloom),
    VKM_F(exposure),
    VKM_F(colorGrade),
    VKM_F(grid),
    VKM_F(aabbDebug),
    VKM_F(selection),
    VKM_F(msaa),
    VKM_F(tonemap),
    VKM_F(clearColor),
    VKM_F(renderMode)
VKM_REFLECT_END()
