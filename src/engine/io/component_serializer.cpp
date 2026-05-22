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
            LOG_WARNING("SceneLoad: mesh asset '%s' not found - Mesh component left unresolved", meshName.c_str());
        }
    }
    if (!materialName.empty()) {
        m.material = resources.findByName<MaterialAsset>(materialName);
        if (!m.material) {
            LOG_WARNING("SceneLoad: material asset '%s' not found - Mesh component left unresolved", materialName.c_str());
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
    return {{"strength", c.strength}};
}
void loadBloom(const nlohmann::json& j, BloomConfig& c) {
    c.strength = j.value("strength", c.strength);
}

nlohmann::json saveExposure(const ExposureConfig& c) {
    return {{"autoExposure", c.autoExposure}, {"key", c.key},
            {"speed", c.speed}, {"min", c.min}, {"max", c.max}};
}
void loadExposure(const nlohmann::json& j, ExposureConfig& c) {
    c.autoExposure = j.value("autoExposure", c.autoExposure);
    c.key          = j.value("key",          c.key);
    c.speed        = j.value("speed",        c.speed);
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
        {"exposure",   saveExposure(e.exposure)},
        {"colorGrade", saveColorGrade(e.colorGrade)},
        {"grid",       saveGrid(e.grid)},
        {"aabbDebug",  saveAABBDebug(e.aabbDebug)},

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
    if (j.contains("exposure"))   loadExposure(j["exposure"],     e.exposure);
    if (j.contains("colorGrade")) loadColorGrade(j["colorGrade"], e.colorGrade);
    if (j.contains("grid"))       loadGrid(j["grid"],             e.grid);
    if (j.contains("aabbDebug"))  loadAABBDebug(j["aabbDebug"],   e.aabbDebug);

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
