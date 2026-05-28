#define VKM_LOG_CATEGORY "IO"

#include "io/component_serializer.h"

#include <cstring>
#include <limits>
#include <string>

#include "logger.h"

#include "ecs/component/camera.h"
#include "ecs/component/transform.h"
#include "ecs/component/reflection_probe.h"
#include "io/reflect.h"
#include "resource/resource_manager.h"
#include "system/render/render_view.h"   // EnvironmentConfig

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

// ---- Generic field <-> JSON converters used by the reflection-driven
// save/load templates. Overload set; ADL is not used (call site qualifies
// nothing), so adding a new type means adding a new overload here.
// Reference parameters where the field is being mutated, by-value for
// trivial inputs.
inline nlohmann::json toJson(bool        v) { return v; }
inline nlohmann::json toJson(int         v) { return v; }
inline nlohmann::json toJson(uint32_t    v) { return v; }
inline nlohmann::json toJson(float       v) { return v; }
inline nlohmann::json toJson(const std::string& s) { return s; }
inline nlohmann::json toJson(const glm::vec2& v) { return vec2ToJson(v); }
inline nlohmann::json toJson(const glm::vec3& v) { return vec3ToJson(v); }
inline nlohmann::json toJson(const glm::vec4& v) { return vec4ToJson(v); }
inline nlohmann::json toJson(const glm::quat& q) { return quatToJson(q); }

// Enums get a per-type overload further down (ProjectionType, ...).

inline void fromJson(const nlohmann::json& j, bool&     v) { v = j.get<bool>(); }
inline void fromJson(const nlohmann::json& j, int&      v) { v = j.get<int>(); }
inline void fromJson(const nlohmann::json& j, uint32_t& v) { v = j.get<uint32_t>(); }
inline void fromJson(const nlohmann::json& j, float&    v) { v = j.get<float>(); }
inline void fromJson(const nlohmann::json& j, std::string& s) { s = j.get<std::string>(); }
inline void fromJson(const nlohmann::json& j, glm::vec2& v) { if (j.is_array() && j.size() >= 2) v = vec2FromJson(j); }
inline void fromJson(const nlohmann::json& j, glm::vec3& v) { if (j.is_array() && j.size() >= 3) v = vec3FromJson(j); }
inline void fromJson(const nlohmann::json& j, glm::vec4& v) { if (j.is_array() && j.size() >= 4) v = vec4FromJson(j); }
inline void fromJson(const nlohmann::json& j, glm::quat& q) { if (j.is_array() && j.size() >= 4) q = quatFromJson(j); }

// Enum support — same overload pattern. ProjectionType is the only one
// currently driven through the reflection path; LightType still has a
// hand-written save/load until the Light component is migrated.
inline nlohmann::json toJson(ProjectionType v) {
    return v == ProjectionType::Perspective ? "Perspective" : "Orthographic";
}
inline void fromJson(const nlohmann::json& j, ProjectionType& v) {
    v = (j.get<std::string>() == "Orthographic")
        ? ProjectionType::Orthographic
        : ProjectionType::Perspective;
}

/// Reflection-driven save: emit one JSON key per declared field.
template<typename T>
nlohmann::json saveReflected(const T& obj) {
    nlohmann::json out = nlohmann::json::object();
    ::Engine::Reflect::forEachField(obj, [&](std::string_view name, const auto& val) {
        out[std::string(name)] = toJson(val);
    });
    return out;
}

/// Reflection-driven load: for each declared field, patch from JSON if
/// present. Missing keys keep the field's current value - that's the
/// forward-compat path when a saved scene predates a new field.
template<typename T>
void loadReflected(const nlohmann::json& j, T& obj) {
    ::Engine::Reflect::forEachField(obj, [&](std::string_view name, auto& val) {
        auto it = j.find(std::string(name));
        if (it != j.end()) fromJson(*it, val);
    });
}

} // namespace

nlohmann::json save(const Name& n) {
    return nlohmann::json{{"value", std::string(n.value)}};
}
void load(const nlohmann::json& j, Name& n) {
    const std::string s = j.value("value", std::string{});
    std::strncpy(n.value, s.c_str(), sizeof(n.value) - 1);
    n.value[sizeof(n.value) - 1] = '\0';
}

// Transform / Camera bodies are now driven by the reflection markup
// (see VKM_REFLECT_* blocks at the bottom of this file). Reflection
// covers the common case: bag-of-fields data structs with primitive,
// glm or enum members. Components with non-trivial shape (Mesh with
// ResourceManager refs, Hierarchy with parent EntityId, Name's fixed
// char buffer, Animation's track vector) keep their hand-written
// save/load below.
nlohmann::json save(const Transform& t) { return saveReflected(t); }
void load(const nlohmann::json& j, Transform& t) { loadReflected(j, t); }

nlohmann::json save(const Camera& c) { return saveReflected(c); }
void load(const nlohmann::json& j, Camera& c) { loadReflected(j, c); }

namespace {
const char* lightTypeName(LightType t) {
    switch (t) {
        case LightType::Directional: return "Directional";
        case LightType::Point:       return "Point";
        case LightType::Spot:        return "Spot";
        case LightType::Rect:        return "Rect";
        case LightType::Disk:        return "Disk";
    }
    return "Directional";
}
LightType lightTypeFromName(const std::string& s) {
    if (s == "Point") return LightType::Point;
    if (s == "Spot")  return LightType::Spot;
    if (s == "Rect")  return LightType::Rect;
    if (s == "Disk")  return LightType::Disk;
    return LightType::Directional;
}
} // namespace

nlohmann::json save(const Light& l) {
    return {
        {"type",           lightTypeName(l.type)},
        {"color",          vec3ToJson(l.color)},
        {"intensity",      l.intensity},
        {"radius",         l.radius},
        {"innerConeAngle", l.innerConeAngle},
        {"outerConeAngle", l.outerConeAngle},
        {"areaWidth",      l.areaWidth},
        {"areaHeight",     l.areaHeight},
        {"areaRadius",     l.areaRadius},
        {"twoSided",       l.twoSided},
        {"castShadows",    l.castShadows},
        {"shadowBias",     l.shadowBias},
        {"shadowExtent",   l.shadowExtent},
        {"enabled",        l.enabled},
    };
}
// bakeVersion is intentionally NOT in the reflection markup, so the
// generic save/load skip it - the backend re-bakes on load and the
// counter starts at 0 again. Fields outside the markup are the
// idiomatic "internal only" path.
nlohmann::json save(const ReflectionProbe& p) { return saveReflected(p); }
void load(const nlohmann::json& j, ReflectionProbe& p) { loadReflected(j, p); }

void load(const nlohmann::json& j, Light& l) {
    l.type           = lightTypeFromName(j.value("type", std::string{"Directional"}));
    l.color          = j.contains("color") ? vec3FromJson(j["color"]) : l.color;
    l.intensity      = j.value("intensity",      l.intensity);
    l.radius         = j.value("radius",         l.radius);
    l.innerConeAngle = j.value("innerConeAngle", l.innerConeAngle);
    l.outerConeAngle = j.value("outerConeAngle", l.outerConeAngle);
    l.areaWidth      = j.value("areaWidth",      l.areaWidth);
    l.areaHeight     = j.value("areaHeight",     l.areaHeight);
    l.areaRadius     = j.value("areaRadius",     l.areaRadius);
    l.twoSided       = j.value("twoSided",       l.twoSided);
    l.castShadows    = j.value("castShadows",    l.castShadows);
    l.shadowBias     = j.value("shadowBias",     l.shadowBias);
    l.shadowExtent   = j.value("shadowExtent",   l.shadowExtent);
    l.enabled        = j.value("enabled",        l.enabled);
}

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

// The singleton "Environment" entity's whole rendering/post stack. Every
// field is a JSON primitive; load uses .value() fallbacks so older saves
// (missing keys) keep the struct defaults and stay forward-compatible.
// EnvironmentConfig is composed of per-effect sub-structs; serialize each
// as its own JSON object so future per-effect versioning (deprecating a
// field in one config) doesn't need to touch unrelated effects' layout.
namespace {

nlohmann::json saveAmbient(const AmbientConfig& c) {
    return {{"color", vec3ToJson(c.color)}, {"intensity", c.intensity}};
}
void loadAmbient(const nlohmann::json& j, AmbientConfig& c) {
    if (j.contains("color")) c.color = vec3FromJson(j["color"]);
    c.intensity = j.value("intensity", c.intensity);
}

nlohmann::json saveIBL(const IBLConfig& c) {
    return {{"path", c.path}, {"intensity", c.intensity}};
}
void loadIBL(const nlohmann::json& j, IBLConfig& c) {
    c.path      = j.value("path",      c.path);
    c.intensity = j.value("intensity", c.intensity);
}

nlohmann::json saveAO(const AOConfig& c) {
    return {{"enabled", c.enabled}, {"radius", c.radius}, {"intensity", c.intensity}};
}
void loadAO(const nlohmann::json& j, AOConfig& c) {
    c.enabled   = j.value("enabled",   c.enabled);
    c.radius    = j.value("radius",    c.radius);
    c.intensity = j.value("intensity", c.intensity);
}

nlohmann::json saveSSR(const SSRConfig& c) {
    return {{"enabled", c.enabled}, {"intensity", c.intensity},
            {"maxDistance", c.maxDistance}, {"thickness", c.thickness}};
}
void loadSSR(const nlohmann::json& j, SSRConfig& c) {
    c.enabled     = j.value("enabled",     c.enabled);
    c.intensity   = j.value("intensity",   c.intensity);
    c.maxDistance = j.value("maxDistance", c.maxDistance);
    c.thickness   = j.value("thickness",   c.thickness);
}

nlohmann::json saveTAA(const TAAConfig& c) {
    return {{"enabled", c.enabled}, {"blend", c.blend}};
}
void loadTAA(const nlohmann::json& j, TAAConfig& c) {
    c.enabled = j.value("enabled", c.enabled);
    c.blend   = j.value("blend",   c.blend);
}

nlohmann::json saveDof(const DofConfig& c) {
    return {{"enabled", c.enabled}, {"focusDistance", c.focusDistance},
            {"focusRange", c.focusRange}, {"maxBlur", c.maxBlur}};
}
void loadDof(const nlohmann::json& j, DofConfig& c) {
    c.enabled       = j.value("enabled",       c.enabled);
    c.focusDistance = j.value("focusDistance", c.focusDistance);
    c.focusRange    = j.value("focusRange",    c.focusRange);
    c.maxBlur       = j.value("maxBlur",       c.maxBlur);
}

nlohmann::json saveMotionBlur(const MotionBlurConfig& c) {
    return {{"enabled", c.enabled}, {"strength", c.strength}};
}
void loadMotionBlur(const nlohmann::json& j, MotionBlurConfig& c) {
    c.enabled  = j.value("enabled",  c.enabled);
    c.strength = j.value("strength", c.strength);
}

nlohmann::json saveStarburst(const StarburstConfig& c) {
    return {{"enabled", c.enabled}, {"intensity", c.intensity}};
}
void loadStarburst(const nlohmann::json& j, StarburstConfig& c) {
    c.enabled   = j.value("enabled",   c.enabled);
    c.intensity = j.value("intensity", c.intensity);
}

nlohmann::json saveLensFlare(const LensFlareConfig& c) {
    return {
        {"enabled",      c.enabled},
        {"intensity",    c.intensity},
        {"threshold",    c.threshold},
        {"ghostCount",   c.ghostCount},
        {"ghostSpacing", c.ghostSpacing},
        {"haloRadius",   c.haloRadius},
        {"chromatic",    c.chromatic},
        {"starburst",    saveStarburst(c.starburst)},
    };
}
void loadLensFlare(const nlohmann::json& j, LensFlareConfig& c) {
    c.enabled      = j.value("enabled",      c.enabled);
    c.intensity    = j.value("intensity",    c.intensity);
    c.threshold    = j.value("threshold",    c.threshold);
    c.ghostCount   = j.value("ghostCount",   c.ghostCount);
    c.ghostSpacing = j.value("ghostSpacing", c.ghostSpacing);
    c.haloRadius   = j.value("haloRadius",   c.haloRadius);
    c.chromatic    = j.value("chromatic",    c.chromatic);
    if (j.contains("starburst")) loadStarburst(j["starburst"], c.starburst);
}

nlohmann::json saveLensDirt(const LensDirtConfig& c) {
    return {{"enabled", c.enabled}, {"intensity", c.intensity}};
}
void loadLensDirt(const nlohmann::json& j, LensDirtConfig& c) {
    c.enabled   = j.value("enabled",   c.enabled);
    c.intensity = j.value("intensity", c.intensity);
}

nlohmann::json saveBloom(const BloomConfig& c) {
    return {{"strength",  c.strength},
            {"threshold", c.threshold},
            {"knee",      c.knee}};
}
void loadBloom(const nlohmann::json& j, BloomConfig& c) {
    c.strength  = j.value("strength",  c.strength);
    c.threshold = j.value("threshold", c.threshold);
    c.knee      = j.value("knee",      c.knee);
}

nlohmann::json saveShadow(const ShadowConfig& c) {
    return {{"atlasRes2D",   c.atlasRes2D},
            {"atlasResCube", c.atlasResCube},
            {"softness",     c.softness}};
}
void loadShadow(const nlohmann::json& j, ShadowConfig& c) {
    c.atlasRes2D   = j.value("atlasRes2D",   c.atlasRes2D);
    c.atlasResCube = j.value("atlasResCube", c.atlasResCube);
    c.softness     = j.value("softness",     c.softness);
}

nlohmann::json saveTransparency(const TransparencyConfig& c) {
    return {{"useOIT", c.useOIT}};
}
void loadTransparency(const nlohmann::json& j, TransparencyConfig& c) {
    c.useOIT = j.value("useOIT", c.useOIT);
}

nlohmann::json saveOcclusion(const OcclusionConfig& c) {
    return {{"useHiZ", c.useHiZ}};
}
void loadOcclusion(const nlohmann::json& j, OcclusionConfig& c) {
    c.useHiZ = j.value("useHiZ", c.useHiZ);
}

nlohmann::json saveExposure(const ExposureConfig& c) {
    return {{"autoExposure",  c.autoExposure},
            {"key",           c.key},
            {"speedBrighten", c.speedBrighten},
            {"speedDarken",   c.speedDarken},
            {"min",           c.min},
            {"max",           c.max}};
}
void loadExposure(const nlohmann::json& j, ExposureConfig& c) {
    c.autoExposure = j.value("autoExposure", c.autoExposure);
    c.key          = j.value("key",          c.key);
    // Backwards compat: scenes saved before the lift/drag split used a
    // single "speed" field. Map it to both rates so old scenes still work.
    if (j.contains("speed") && !j.contains("speedBrighten")) {
        c.speedBrighten = j.value("speed", c.speedBrighten);
        c.speedDarken   = j.value("speed", c.speedDarken);
    } else {
        c.speedBrighten = j.value("speedBrighten", c.speedBrighten);
        c.speedDarken   = j.value("speedDarken",   c.speedDarken);
    }
    c.min          = j.value("min",          c.min);
    c.max          = j.value("max",          c.max);
}

nlohmann::json saveColorGrade(const ColorGradeConfig& c) {
    return {{"enabled", c.enabled}, {"lutPath", c.lutPath}, {"intensity", c.intensity}};
}
void loadColorGrade(const nlohmann::json& j, ColorGradeConfig& c) {
    c.enabled   = j.value("enabled",   c.enabled);
    c.lutPath   = j.value("lutPath",   c.lutPath);
    c.intensity = j.value("intensity", c.intensity);
}

nlohmann::json saveGrid(const GridConfig& c) {
    return {{"enabled", c.enabled}, {"size", c.size}, {"scale", c.scale},
            {"fadeStart", c.fadeStart}, {"fadeEnd", c.fadeEnd}};
}
void loadGrid(const nlohmann::json& j, GridConfig& c) {
    c.enabled   = j.value("enabled",   c.enabled);
    c.size      = j.value("size",      c.size);
    c.scale     = j.value("scale",     c.scale);
    c.fadeStart = j.value("fadeStart", c.fadeStart);
    c.fadeEnd   = j.value("fadeEnd",   c.fadeEnd);
}

nlohmann::json saveAABBDebug(const AABBDebugConfig& c) {
    return {{"enabled", c.enabled}, {"color", vec3ToJson(c.color)}};
}
void loadAABBDebug(const nlohmann::json& j, AABBDebugConfig& c) {
    c.enabled = j.value("enabled", c.enabled);
    if (j.contains("color")) c.color = vec3FromJson(j["color"]);
}

nlohmann::json saveSelection(const SelectionOutlineConfig& c) {
    return {{"enabled", c.enabled},
            {"color", vec3ToJson(c.color)},
            {"thickness", c.thickness}};
}
void loadSelection(const nlohmann::json& j, SelectionOutlineConfig& c) {
    c.enabled   = j.value("enabled", c.enabled);
    c.thickness = j.value("thickness", c.thickness);
    if (j.contains("color")) c.color = vec3FromJson(j["color"]);
}

} // namespace

nlohmann::json save(const EnvironmentConfig& e) {
    return {
        {"ambient",    saveAmbient(e.ambient)},
        {"ibl",        saveIBL(e.ibl)},
        {"ao",         saveAO(e.ao)},
        {"ssr",        saveSSR(e.ssr)},
        {"taa",        saveTAA(e.taa)},
        {"dof",        saveDof(e.dof)},
        {"motionBlur", saveMotionBlur(e.motionBlur)},
        {"lensFlare",  saveLensFlare(e.lensFlare)},
        {"lensDirt",   saveLensDirt(e.lensDirt)},
        {"bloom",      saveBloom(e.bloom)},
        {"shadow",     saveShadow(e.shadow)},
        {"transparency", saveTransparency(e.transparency)},
        {"occlusion",  saveOcclusion(e.occlusion)},
        {"exposure",   saveExposure(e.exposure)},
        {"colorGrade", saveColorGrade(e.colorGrade)},
        {"grid",       saveGrid(e.grid)},
        {"aabbDebug",  saveAABBDebug(e.aabbDebug)},
        {"selection",  saveSelection(e.selection)},

        {"tonemap",    e.tonemap},
        {"clearColor", vec4ToJson(e.clearColor)},
        {"renderMode", static_cast<int>(e.renderMode)},
    };
}

void load(const nlohmann::json& j, EnvironmentConfig& e) {
    if (j.contains("ambient"))    loadAmbient(j["ambient"],       e.ambient);
    if (j.contains("ibl"))        loadIBL(j["ibl"],               e.ibl);
    if (j.contains("ao"))         loadAO(j["ao"],                 e.ao);
    if (j.contains("ssr"))        loadSSR(j["ssr"],               e.ssr);
    if (j.contains("taa"))        loadTAA(j["taa"],               e.taa);
    if (j.contains("dof"))        loadDof(j["dof"],               e.dof);
    if (j.contains("motionBlur")) loadMotionBlur(j["motionBlur"], e.motionBlur);
    if (j.contains("lensFlare"))  loadLensFlare(j["lensFlare"],   e.lensFlare);
    if (j.contains("lensDirt"))   loadLensDirt(j["lensDirt"],     e.lensDirt);
    if (j.contains("bloom"))      loadBloom(j["bloom"],           e.bloom);
    if (j.contains("shadow"))     loadShadow(j["shadow"],         e.shadow);
    if (j.contains("transparency")) loadTransparency(j["transparency"], e.transparency);
    if (j.contains("occlusion"))  loadOcclusion(j["occlusion"],   e.occlusion);
    if (j.contains("exposure"))   loadExposure(j["exposure"],     e.exposure);
    if (j.contains("colorGrade")) loadColorGrade(j["colorGrade"], e.colorGrade);
    if (j.contains("grid"))       loadGrid(j["grid"],             e.grid);
    if (j.contains("aabbDebug"))  loadAABBDebug(j["aabbDebug"],   e.aabbDebug);
    if (j.contains("selection"))  loadSelection(j["selection"],   e.selection);

    e.tonemap   = j.value("tonemap",   e.tonemap);
    if (j.contains("clearColor")) e.clearColor = vec4FromJson(j["clearColor"]);

    // RenderMode replaced the old `wireframe` bool. Read the new int form
    // first; fall back to the legacy bool so scenes saved before the
    // refactor still load with their diagnostic view intact.
    if (j.contains("renderMode")) {
        e.renderMode = static_cast<RenderMode>(j["renderMode"].get<int>());
    } else if (j.value("wireframe", false)) {
        e.renderMode = RenderMode::Wireframe;
    }
}

} // namespace Engine::ComponentSerializer

// ----------------------------------------------------------------------------
// Reflection markups. Listed at file scope so the VKM_REFLECT_BEGIN macro
// can re-open Engine::Reflect and specialise Traits<T> there without
// fighting the surrounding namespace. Each entry names the JSON-persisted
// fields; fields omitted here are NOT serialised (the bakeVersion pattern
// in ReflectionProbe relies on this).
// ----------------------------------------------------------------------------

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

VKM_REFLECT_BEGIN(Engine::ReflectionProbe)
    VKM_F(hdrPath),
    VKM_F(radius),
    VKM_F(falloffRange),
    VKM_F(intensity)
    // bakeVersion is intentionally absent - see save(ReflectionProbe).
VKM_REFLECT_END()
