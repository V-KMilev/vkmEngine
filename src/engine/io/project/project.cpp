#define VKM_LOG_CATEGORY "IO"

#include "io/project/project.h"

#include <algorithm>
#include <climits>
#include <stdexcept>
#include <type_traits>
#include <system_error>

#include <nlohmann/json.hpp>

#include "ecs/component/physics/rigidbody.h"
#include "io/json/json_file.h"
#include "io/json/json_number.h"
#include "io/project/actions_json.h"
#include "io/json/reflect_json.h"
#include "vkm_log/logger.h"

namespace fs = std::filesystem;

namespace Vkm::Engine {

static_assert(
    Project::MAX_PHYSICS_LAYERS == sizeof(Rigidbody::layer) * CHAR_BIT,
    "a physics layer for each bit of a Rigidbody's layer"
);

namespace {

constexpr const char* PROJECT_FILE = "project.json";

// A directory as one absolute, normal path ending in its own name. "proj/" ends in an empty
// element and "proj/." in a dot; both compose away, so "logs/<project>/" built on one would lose
// its <project>, and a project would take no name from its folder.
fs::path normalDirectory(const fs::path& dir) {
    std::error_code ec;
    fs::path normal = fs::absolute(dir, ec).lexically_normal();
    if (ec) normal = dir.lexically_normal();
    if (!normal.has_filename()) normal = normal.parent_path();
    return normal;
}

/**
 * @brief Read each entry of @p doc's array @p list with @p readEntry; one it throws on is skipped,
 *        with a warning when @p report, as a wrong scalar is: the rest still reads.
 *
 * @param doc       A project.json.
 * @param list      The array's key.
 * @param report    Whether a skipped entry is warned of.
 * @param readEntry Called with each entry; throws to skip it.
 */
template<typename ReadEntry>
void eachEntry(const nlohmann::json& doc, const char* list, bool report, const ReadEntry& readEntry) {
    if (!doc.contains(list) || !doc[list].is_array()) return;
    for (const nlohmann::json& entry : doc[list]) {
        try {
            if (!entry.is_object()) throw std::invalid_argument("not an object");
            readEntry(entry);
        } catch (const std::exception& e) {
            if (report) LOG_WARNING("A %s entry %s is skipped: %s", list, entry.dump().c_str(), e.what());
        }
    }
}

/// The fonts @p doc lists, as loadProject reads them; @p report warns of each one skipped.
std::vector<FontEntry> fontsIn(const nlohmann::json& doc, bool report) {
    std::vector<FontEntry> fonts;
    eachEntry(doc, "fonts", report, [&](const nlohmann::json& entry) {
        FontEntry font;
        ReflectJson::loadFields(entry, font, ReflectJson::PlainData{});
        if (font.name.empty() || font.file.empty()) {
            throw std::invalid_argument("a font wants a name and a file");
        }
        fonts.push_back(std::move(font));
    });
    return fonts;
}

/**
 * @brief The logos @p doc lists, as loadProject reads them: one with no image is skipped rather
 *        than shown as a black pause.
 */
std::vector<SplashEntry> splashIn(const nlohmann::json& doc, bool report) {
    std::vector<SplashEntry> splash;
    eachEntry(doc, "splash", report, [&](const nlohmann::json& entry) {
        SplashEntry logo;
        ReflectJson::loadFields(entry, logo, ReflectJson::PlainData{});
        if (!logo.image.empty()) splash.push_back(std::move(logo));
    });
    return splash;
}

/// The loading screen @p doc gives, as loadProject reads it: past tips with no text.
LoadingScreen loadingIn(const nlohmann::json& doc) {
    LoadingScreen loading;
    if (const auto found = doc.find("loading"); found != doc.end()) ReflectJson::fromJson(*found, loading);
    loading.tips.erase(std::remove(loading.tips.begin(), loading.tips.end(), ""), loading.tips.end());
    return loading;
}

} // namespace

bool loadProject(const fs::path& projectRoot, Project& out) {
    const fs::path file = projectRoot / PROJECT_FILE;
    out.name = normalDirectory(projectRoot).filename().string();  // unless the file names it

    std::error_code ec;
    if (!fs::exists(file, ec)) {
        LOG_INFO("No %s in '%s'; running it on the defaults", PROJECT_FILE, projectRoot.string().c_str());
        return false;
    }

    nlohmann::json doc;
    if (!detail::readJsonFile(file, doc, "project")) return false;

    // Each field on its own: one of the wrong type keeps its default without abandoning the rest,
    // which a tool saving the file afterwards would write over.
    const std::string folder = out.name;
    ReflectJson::fromJson(doc, out);
    if (out.name.empty()) out.name = folder;
    // Clamped here, not at each reader: the clock would not honor an unclamped value.
    const uint32_t askedTick = out.tickRate;
    out.tickRate = Config::clampTickRate(askedTick);
    if (out.tickRate != askedTick) {
        LOG_WARNING("Project asks for %u ticks a second; running at %u", askedTick, out.tickRate);
    }
    // Clamped here rather than at each reader, as tickRate is.
    const uint32_t askedSeats = out.maxPlayers;
    out.maxPlayers = Config::clampSeats(askedSeats);
    if (out.maxPlayers != askedSeats) {
        LOG_WARNING("Project asks for %u seats; serving %u", askedSeats, out.maxPlayers);
    }
    if (doc.contains("actions")) out.actions = actionsFromJson(doc["actions"]);
    // Read a name at a time: a layer is its place in the list, so one that is no name stays,
    // unnamed, rather than moving every layer after it onto another bit.
    if (doc.contains("physicsLayers") && doc["physicsLayers"].is_array()) {
        out.physicsLayers.clear();
        for (const nlohmann::json& entry : doc["physicsLayers"]) {
            if (!entry.is_string()) {
                const std::string held = entry.dump();
                LOG_WARNING("project.json physicsLayers holds '%s', not a name: unnamed", held.c_str());
            }
            out.physicsLayers.push_back(entry.is_string() ? entry.get<std::string>() : std::string());
        }
    } else if (doc.contains("physicsLayers")) {
        LOG_WARNING("project.json physicsLayers is not a list of names; ignored");
    }
    if (out.physicsLayers.size() > Project::MAX_PHYSICS_LAYERS) {
        LOG_WARNING(
            "project.json names %zu physics layers; a body has %zu bits, so the rest are dropped",
            out.physicsLayers.size(),
            Project::MAX_PHYSICS_LAYERS
        );
        out.physicsLayers.resize(Project::MAX_PHYSICS_LAYERS);
    }

    if (doc.contains("render")) {
        // See RenderSettings::MIN_RENDER_SCALE and isMsaaSampleCount.
        const float scale = std::clamp(
            out.render.renderScale,
            RenderSettings::MIN_RENDER_SCALE,
            RenderSettings::MAX_RENDER_SCALE
        );
        if (scale != out.render.renderScale) {
            LOG_WARNING(
                "project.json render.renderScale %g is outside %g..%g; using %g",
                out.render.renderScale,
                RenderSettings::MIN_RENDER_SCALE,
                RenderSettings::MAX_RENDER_SCALE,
                scale
            );
            out.render.renderScale = scale;
        }
        if (!RenderSettings::isMsaaSampleCount(out.render.msaaSamples)) {
            LOG_WARNING(
                "project.json render.msaaSamples %u is not 1, 2, 4 or 8; using %u",
                out.render.msaaSamples,
                RenderSettings{}.msaaSamples
            );
            out.render.msaaSamples = RenderSettings{}.msaaSamples;
        }
    }

    out.fonts   = fontsIn(doc, true);
    out.splash  = splashIn(doc, true);
    out.loading = loadingIn(doc);

    if (out.engineVersion.empty()) {
        LOG_INFO("Project '%s' (engine version unrecorded)", out.name.c_str());
    } else if (compatibleEngine(out.engineVersion)) {
        LOG_INFO("Project '%s' (engine %s)", out.name.c_str(), out.engineVersion.c_str());
    } else {
        LOG_WARNING(
            "Project '%s' was authored against engine %s; this is %s",
            out.name.c_str(),
            out.engineVersion.c_str(),
            APP_VERSION
        );
    }
    return true;
}

namespace {

// project.json as it is, for a write to keep what it does not set: empty when there is none yet,
// and false when one is there that does not parse, which a write would wipe.
bool readForWrite(const fs::path& file, nlohmann::json& doc) {
    std::error_code ec;
    doc = nlohmann::json::object();
    if (!fs::exists(file, ec)) return true;
    if (detail::readJsonFile(file, doc, "project") && doc.is_object()) return true;
    LOG_ERROR("'%s' does not parse; not written over, so mend it by hand", file.string().c_str());
    return false;
}

// What of @p value differs from @p defaults, key by key and inside a nested block too: a field at
// its default is no key, so the file says what the project chose and nothing else.
nlohmann::json changedFrom(const nlohmann::json& value, const nlohmann::json& defaults) {
    nlohmann::json changed = nlohmann::json::object();
    for (auto it = value.begin(); it != value.end(); ++it) {
        const auto base = defaults.find(it.key());
        if (base != defaults.end() && it->is_object() && base->is_object()) {
            nlohmann::json inner = changedFrom(*it, *base);
            if (!inner.empty()) changed[it.key()] = std::move(inner);
        } else if (base == defaults.end() || *it != *base) {
            changed[it.key()] = *it;
        }
    }
    return changed;
}

} // namespace

bool saveProject(const fs::path& projectRoot, const Project& project) {
    const fs::path file = projectRoot / PROJECT_FILE;
    nlohmann::json doc;
    if (!readForWrite(file, doc)) return false;

    const nlohmann::json fields  = ReflectJson::toJson(project);
    const nlohmann::json changed = changedFrom(fields, ReflectJson::toJson(Project{}));
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        if (changed.contains(it.key())) doc[it.key()] = changed[it.key()];
        else                           doc.erase(it.key());
    }
    // Written only when named, so a project that never named one keeps a lean file.
    std::vector<std::string> layers = project.physicsLayers;
    while (!layers.empty() && layers.back().empty()) layers.pop_back();
    if (layers.empty()) doc.erase("physicsLayers");
    else                doc["physicsLayers"] = layers;

    // As the lists below: left as written while it reads the same, entries loadProject skips and all.
    const nlohmann::json actions = actionsToJson(project.actions);
    if (actionsToJson(actionsFromJson(doc.value("actions", nlohmann::json::object()))) != actions) {
        doc["actions"] = actions;
        if (project.actions.empty()) doc.erase("actions");
    }

    // A list as the file already reads is left as written, entries loadProject skips and all, and so
    // is the loading screen; an empty one is no key.
    if (fontsIn(doc, false) != project.fonts) {
        doc["fonts"] = ReflectJson::toJson(project.fonts);
        if (project.fonts.empty()) doc.erase("fonts");
    }
    if (splashIn(doc, false) != project.splash) {
        doc["splash"] = ReflectJson::toJson(project.splash);
        if (project.splash.empty()) doc.erase("splash");
    }
    if (loadingIn(doc) != project.loading) {
        doc["loading"] = ReflectJson::toJson(project.loading);
        if (project.loading == LoadingScreen{}) doc.erase("loading");
    }

    return detail::writeJsonFile(file, doc, "project");
}

bool saveProjectRender(const fs::path& projectRoot, const RenderSettings& render) {
    const fs::path file = projectRoot / PROJECT_FILE;
    nlohmann::json doc;
    if (!readForWrite(file, doc)) return false;
    doc["render"] = changedFrom(ReflectJson::toJson(render), ReflectJson::toJson(RenderSettings{}));
    if (doc["render"].empty()) doc.erase("render");
    return detail::writeJsonFile(file, doc, "project");
}

bool saveProjectEngine(const fs::path& projectRoot) {
    const fs::path file = projectRoot / PROJECT_FILE;
    nlohmann::json doc;
    if (!readForWrite(file, doc)) return false;
    doc["engineVersion"] = APP_VERSION;
    return detail::writeJsonFile(file, doc, "project");
}

bool setEntrySceneIfNone(const fs::path& projectRoot, const std::string& scene) {
    const fs::path file = projectRoot / PROJECT_FILE;
    nlohmann::json doc;
    if (!isProjectRoot(projectRoot) || !readForWrite(file, doc)) return false;
    const auto named = doc.find("entryScene");
    if (named != doc.end() && *named != "") return false;
    doc["entryScene"] = scene;
    return detail::writeJsonFile(file, doc, "project");
}

bool isProjectRoot(const fs::path& directory) {
    std::error_code ec;
    return fs::exists(directory / PROJECT_FILE, ec);
}

bool sameProjectRender(const RenderSettings& a, const RenderSettings& b) {
    return ReflectJson::toJson(a) == ReflectJson::toJson(b);
}

bool compatibleEngine(const std::string& engineVersion) {
    if (engineVersion.empty()) return true;
    // "1.0" of "1.0.3": up to the second dot, which a bare "1.0" lacks.
    const auto release = [](const std::string& version) {
        const size_t first = version.find('.');
        return first == std::string::npos ? version : version.substr(0, version.find('.', first + 1));
    };
    return release(engineVersion) == release(APP_VERSION);
}

fs::path findProjectRoot(const fs::path& start) {
    std::error_code ec;

    fs::path dir = normalDirectory(fs::is_directory(start, ec) ? start : start.parent_path());

    while (!dir.empty()) {
        if (isProjectRoot(dir)) return dir;
        const fs::path parent = dir.parent_path();
        if (parent == dir) break;  // reached the filesystem root
        dir = parent;
    }
    return {};
}

} // namespace Vkm::Engine
