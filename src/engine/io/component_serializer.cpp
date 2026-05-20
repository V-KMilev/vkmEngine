#include "io/component_serializer.h"

#include <cstring>
#include <limits>

#include "logger.h"

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

} // namespace

nlohmann::json save(const Name& n) {
    return nlohmann::json{{"value", std::string(n.value)}};
}
void load(const nlohmann::json& j, Name& n) {
    const std::string s = j.value("value", std::string{});
    std::strncpy(n.value, s.c_str(), sizeof(n.value) - 1);
    n.value[sizeof(n.value) - 1] = '\0';
}

nlohmann::json save(const Transform& t) {
    return {
        {"position", vec3ToJson(t.position)},
        {"rotation", quatToJson(t.rotation)},
        {"scale",    vec3ToJson(t.scale)},
    };
}
void load(const nlohmann::json& j, Transform& t) {
    if (j.contains("position")) t.position = vec3FromJson(j["position"]);
    if (j.contains("rotation")) t.rotation = quatFromJson(j["rotation"]);
    if (j.contains("scale"))    t.scale    = vec3FromJson(j["scale"]);
}

nlohmann::json save(const Camera& c) {
    return {
        {"projection",   c.projection == ProjectionType::Perspective ? "Perspective" : "Orthographic"},
        {"fovY",         c.fovY},
        {"orthoHeight",  c.orthoHeight},
        {"aspect",       c.aspect},
        {"zNear",        c.zNear},
        {"zFar",         c.zFar},
        {"exposure",     c.exposure},
        {"active",       c.active},
    };
}
void load(const nlohmann::json& j, Camera& c) {
    const std::string proj = j.value("projection", std::string{"Perspective"});
    c.projection  = (proj == "Orthographic") ? ProjectionType::Orthographic : ProjectionType::Perspective;
    c.fovY        = j.value("fovY",        c.fovY);
    c.orthoHeight = j.value("orthoHeight", c.orthoHeight);
    c.aspect      = j.value("aspect",      c.aspect);
    c.zNear       = j.value("zNear",       c.zNear);
    c.zFar        = j.value("zFar",        c.zFar);
    c.exposure    = j.value("exposure",    c.exposure);
    c.active      = j.value("active",      c.active);
}

namespace {
const char* lightTypeName(LightType t) {
    switch (t) {
        case LightType::Directional: return "Directional";
        case LightType::Point:       return "Point";
        case LightType::Spot:        return "Spot";
    }
    return "Directional";
}
LightType lightTypeFromName(const std::string& s) {
    if (s == "Point") return LightType::Point;
    if (s == "Spot")  return LightType::Spot;
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
        {"castShadows",    l.castShadows},
        {"shadowBias",     l.shadowBias},
        {"shadowExtent",   l.shadowExtent},
        {"enabled",        l.enabled},
    };
}
void load(const nlohmann::json& j, Light& l) {
    l.type           = lightTypeFromName(j.value("type", std::string{"Directional"}));
    l.color          = j.contains("color") ? vec3FromJson(j["color"]) : l.color;
    l.intensity      = j.value("intensity",      l.intensity);
    l.radius         = j.value("radius",         l.radius);
    l.innerConeAngle = j.value("innerConeAngle", l.innerConeAngle);
    l.outerConeAngle = j.value("outerConeAngle", l.outerConeAngle);
    l.castShadows    = j.value("castShadows",    l.castShadows);
    l.shadowBias     = j.value("shadowBias",     l.shadowBias);
    l.shadowExtent   = j.value("shadowExtent",   l.shadowExtent);
    l.enabled        = j.value("enabled",        l.enabled);
}

nlohmann::json save(const Mesh& m, const ResourceManager& resources) {
    const std::string meshName     = m.mesh     ? resources.get(m.mesh).name     : "";
    const std::string materialName = m.material ? resources.get(m.material).name : "";
    return {
        {"mesh",        meshName},
        {"material",    materialName},
        {"visible",     m.visible},
        {"castShadows", m.castShadows},
    };
}
void load(const nlohmann::json& j, Mesh& m, const ResourceManager& resources) {
    const std::string meshName     = j.value("mesh",     std::string{});
    const std::string materialName = j.value("material", std::string{});

    if (!meshName.empty()) {
        m.mesh = resources.findByName<MeshAsset>(meshName);
        if (!m.mesh) {
            LOG_WARNING("SceneLoad: mesh asset '%s' not found — Mesh component left unresolved", meshName.c_str());
        }
    }
    if (!materialName.empty()) {
        m.material = resources.findByName<MaterialAsset>(materialName);
        if (!m.material) {
            LOG_WARNING("SceneLoad: material asset '%s' not found — Mesh component left unresolved", materialName.c_str());
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
nlohmann::json save(const EnvironmentConfig& e) {
    return {
        {"ambientColor",        vec3ToJson(e.ambientColor)},
        {"ambientIntensity",    e.ambientIntensity},
        {"environmentMapPath",  e.environmentMapPath},
        {"iblIntensity",        e.iblIntensity},
        {"ssao",                e.ssao},
        {"ssaoRadius",          e.ssaoRadius},
        {"ssaoIntensity",       e.ssaoIntensity},
        {"ssr",                 e.ssr},
        {"ssrIntensity",        e.ssrIntensity},
        {"ssrMaxDistance",      e.ssrMaxDistance},
        {"ssrThickness",        e.ssrThickness},
        {"taa",                 e.taa},
        {"taaBlend",            e.taaBlend},
        {"dof",                 e.dof},
        {"dofFocusDistance",    e.dofFocusDistance},
        {"dofFocusRange",       e.dofFocusRange},
        {"dofMaxBlur",          e.dofMaxBlur},
        {"motionBlur",          e.motionBlur},
        {"motionBlurStrength",  e.motionBlurStrength},
        {"colorGrade",          e.colorGrade},
        {"colorLutPath",        e.colorLutPath},
        {"colorGradeIntensity", e.colorGradeIntensity},
        {"tonemap",             e.tonemap},
        {"bloomStrength",       e.bloomStrength},
        {"autoExposure",        e.autoExposure},
        {"exposureKey",         e.exposureKey},
        {"exposureSpeed",       e.exposureSpeed},
        {"exposureMin",         e.exposureMin},
        {"exposureMax",         e.exposureMax},
        {"clearColor",          vec4ToJson(e.clearColor)},
        {"gridEnabled",         e.gridEnabled},
        {"gridSize",            e.gridSize},
        {"gridScale",           e.gridScale},
        {"gridFadeStart",       e.gridFadeStart},
        {"gridFadeEnd",         e.gridFadeEnd},
        {"aabbDebug",           e.aabbDebug},
        {"debugColor",          vec3ToJson(e.debugColor)},
        {"wireframe",           e.wireframe},
    };
}
void load(const nlohmann::json& j, EnvironmentConfig& e) {
    if (j.contains("ambientColor")) e.ambientColor = vec3FromJson(j["ambientColor"]);
    e.ambientIntensity    = j.value("ambientIntensity",    e.ambientIntensity);
    e.environmentMapPath  = j.value("environmentMapPath",  e.environmentMapPath);
    e.iblIntensity        = j.value("iblIntensity",        e.iblIntensity);
    e.ssao                = j.value("ssao",                e.ssao);
    e.ssaoRadius          = j.value("ssaoRadius",          e.ssaoRadius);
    e.ssaoIntensity       = j.value("ssaoIntensity",       e.ssaoIntensity);
    e.ssr                 = j.value("ssr",                 e.ssr);
    e.ssrIntensity        = j.value("ssrIntensity",        e.ssrIntensity);
    e.ssrMaxDistance      = j.value("ssrMaxDistance",      e.ssrMaxDistance);
    e.ssrThickness        = j.value("ssrThickness",        e.ssrThickness);
    e.taa                 = j.value("taa",                 e.taa);
    e.taaBlend            = j.value("taaBlend",            e.taaBlend);
    e.dof                 = j.value("dof",                 e.dof);
    e.dofFocusDistance    = j.value("dofFocusDistance",    e.dofFocusDistance);
    e.dofFocusRange       = j.value("dofFocusRange",       e.dofFocusRange);
    e.dofMaxBlur          = j.value("dofMaxBlur",          e.dofMaxBlur);
    e.motionBlur          = j.value("motionBlur",          e.motionBlur);
    e.motionBlurStrength  = j.value("motionBlurStrength",  e.motionBlurStrength);
    e.colorGrade          = j.value("colorGrade",          e.colorGrade);
    e.colorLutPath        = j.value("colorLutPath",        e.colorLutPath);
    e.colorGradeIntensity = j.value("colorGradeIntensity", e.colorGradeIntensity);
    e.tonemap             = j.value("tonemap",             e.tonemap);
    e.bloomStrength       = j.value("bloomStrength",       e.bloomStrength);
    e.autoExposure        = j.value("autoExposure",        e.autoExposure);
    e.exposureKey         = j.value("exposureKey",         e.exposureKey);
    e.exposureSpeed       = j.value("exposureSpeed",       e.exposureSpeed);
    e.exposureMin         = j.value("exposureMin",         e.exposureMin);
    e.exposureMax         = j.value("exposureMax",         e.exposureMax);
    if (j.contains("clearColor")) e.clearColor = vec4FromJson(j["clearColor"]);
    e.gridEnabled         = j.value("gridEnabled",         e.gridEnabled);
    e.gridSize            = j.value("gridSize",            e.gridSize);
    e.gridScale           = j.value("gridScale",           e.gridScale);
    e.gridFadeStart       = j.value("gridFadeStart",       e.gridFadeStart);
    e.gridFadeEnd         = j.value("gridFadeEnd",         e.gridFadeEnd);
    e.aabbDebug           = j.value("aabbDebug",           e.aabbDebug);
    if (j.contains("debugColor")) e.debugColor = vec3FromJson(j["debugColor"]);
    e.wireframe           = j.value("wireframe",           e.wireframe);
}

} // namespace Engine::ComponentSerializer
