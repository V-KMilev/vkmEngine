#define VKM_LOG_CATEGORY "IO"

#include "io/project.h"

#include <algorithm>
#include <climits>
#include <fstream>
#include <stdexcept>
#include <type_traits>
#include <system_error>

#include <nlohmann/json.hpp>

#include "ecs/component/physics/rigidbody.h"
#include "io/json_file.h"
#include "logger.h"

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
        font.name = entry.value("name", std::string{});
        font.file = entry.value("file", std::string{});
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
        logo.image   = entry.value("image", std::string{});
        logo.seconds = entry.value("seconds", logo.seconds);
        if (!logo.image.empty()) splash.push_back(std::move(logo));
    });
    return splash;
}

/// The loading screen @p doc gives, as loadProject reads it; @p report warns of what it skips.
LoadingScreen loadingIn(const nlohmann::json& doc, bool report) {
    LoadingScreen loading;
    const auto found = doc.find("loading");
    if (found == doc.end()) return loading;
    if (!found->is_object()) {
        if (report) LOG_WARNING("project.json loading is not an object; ignored");
        return loading;
    }
    const nlohmann::json& in = *found;
    if (in.contains("tips") && in["tips"].is_array()) {
        for (const nlohmann::json& tip : in["tips"]) {
            if (tip.is_string() && !tip.get<std::string>().empty()) {
                loading.tips.push_back(tip.get<std::string>());
            } else if (!tip.is_string() && report) {
                LOG_WARNING("project.json loading.tips holds %s, not text; skipped", tip.dump().c_str());
            }
        }
    } else if (in.contains("tips") && report) {
        LOG_WARNING("project.json loading.tips is not a list; ignored");
    }
    if (in.contains("screen") && in["screen"].is_string()) {
        loading.screen = in["screen"].get<std::string>();
    } else if (in.contains("screen") && report) {
        LOG_WARNING("project.json loading.screen is not a path; ignored");
    }
    if (in.contains("water") && in["water"].is_boolean()) {
        loading.water = in["water"].get<bool>();
    } else if (in.contains("water") && report) {
        LOG_WARNING("project.json loading.water is not true or false; ignored");
    }
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

    std::ifstream in(file);
    if (!in) {
        LOG_ERROR("Cannot open '%s'", file.string().c_str());
        return false;
    }

    // The field reads are inside the try: value<std::string> throws on a key
    // holding a number or an array.
    nlohmann::json doc;
    try {
        in >> doc;
        // Each key on its own: one of the wrong type keeps its default without abandoning the rest,
        // which a tool saving the file afterwards would write over.
        const auto read = [&doc](const char* key, auto& field) {
            if (!doc.contains(key)) return;
            try {
                field = doc[key].get<std::decay_t<decltype(field)>>();
            } catch (const nlohmann::json::exception&) {
                const std::string held = doc[key].dump();
                LOG_WARNING("project.json %s is '%s', not a value of its type; ignored", key, held.c_str());
            }
        };
        read("description",   out.description);
        read("version",       out.version);
        read("engineVersion", out.engineVersion);
        read("entryScene",    out.entryScene);
        std::string named;
        read("name", named);
        if (!named.empty()) out.name = named;
        // Clamped here, not at each reader: the clock would not honour an unclamped value.
        uint32_t askedTick = out.tickRate;
        read("tickRate", askedTick);
        out.tickRate = Config::clampTickRate(askedTick);
        if (out.tickRate != askedTick) {
            LOG_WARNING("Project asks for %u ticks a second; running at %u", askedTick, out.tickRate);
        }
        // Clamped here rather than at each reader, as tickRate is.
        uint32_t askedSeats = out.maxPlayers;
        read("maxPlayers", askedSeats);
        out.maxPlayers = Config::clampSeats(askedSeats);
        if (out.maxPlayers != askedSeats) {
            LOG_WARNING("Project asks for %u seats; serving %u", askedSeats, out.maxPlayers);
        }
        read("netPort", out.netPort);
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
        } else {
            read("physicsLayers", out.physicsLayers);
        }
        if (out.physicsLayers.size() > Project::MAX_PHYSICS_LAYERS) {
            LOG_WARNING(
                "project.json names %zu physics layers; a body has %zu bits, so the rest are dropped",
                out.physicsLayers.size(),
                Project::MAX_PHYSICS_LAYERS
            );
            out.physicsLayers.resize(Project::MAX_PHYSICS_LAYERS);
        }

        // Absent fields keep their defaults.
        if (doc.contains("render") && doc["render"].is_object()) {
            const nlohmann::json& render = doc["render"];
            visitShippedRenderFields(out.render, [&](const char* key, auto& field) {
                using Field = std::decay_t<decltype(field)>;
                if (!render.contains(key)) return;
                if constexpr (std::is_enum_v<Field>) {
                    static_assert(Reflect::HAS_ENUM_NAMES<Field>, "a shipped enum travels as its name");
                    if (!render[key].is_string()) {
                        LOG_WARNING(
                            "project.json render.%s wants the setting's name as a string; keeping '%s'",
                            key,
                            Reflect::enumName(field)
                        );
                    } else if (const std::string asked = render[key].get<std::string>();
                        !Reflect::enumFromNameChecked(asked, field)) {
                        LOG_WARNING(
                            "project.json render.%s names '%s', which this build has no setting for; "
                                "keeping '%s'",
                            key,
                            asked.c_str(),
                            Reflect::enumName(field)
                        );
                    }
                } else {
                    // Per field: a mistyped value keeps its current one without
                    // abandoning the rest of the file.
                    try {
                        field = render[key].get<Field>();
                    } catch (const nlohmann::json::exception&) {
                        LOG_WARNING(
                            "project.json render.%s is '%s', which is not a value of its type; "
                                "keeping the current one",
                            key,
                            render[key].dump().c_str()
                        );
                    }
                }
            });

            // See RenderSettings::MIN_GTAO_RADIUS, MIN_RENDER_SCALE and isMsaaSampleCount.
            const float radius = std::clamp(
                out.render.gtaoRadius,
                RenderSettings::MIN_GTAO_RADIUS,
                RenderSettings::MAX_GTAO_RADIUS
            );
            if (radius != out.render.gtaoRadius) {
                LOG_WARNING(
                    "project.json render.gtaoRadius %g is outside %g..%g; using %g",
                    out.render.gtaoRadius,
                    RenderSettings::MIN_GTAO_RADIUS,
                    RenderSettings::MAX_GTAO_RADIUS,
                    radius
                );
                out.render.gtaoRadius = radius;
            }
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
        out.loading = loadingIn(doc, true);
    } catch (const std::exception& e) {
        LOG_ERROR("Malformed '%s': %s", file.string().c_str(), e.what());
        return false;
    }
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

// The render block as project.json holds it: the shipped fields, an enum by its name.
nlohmann::json renderJson(const RenderSettings& settings) {
    nlohmann::json render = nlohmann::json::object();
    visitShippedRenderFields(settings, [&](const char* key, auto& field) {
        using Field = std::decay_t<decltype(field)>;
        if constexpr (std::is_enum_v<Field>) {
            static_assert(Reflect::HAS_ENUM_NAMES<Field>, "a shipped enum travels as its name");
            render[key] = Reflect::enumName(field);
        } else {
            render[key] = field;
        }
    });
    return render;
}

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

} // namespace

bool saveProject(const fs::path& projectRoot, const Project& project) {
    const fs::path file = projectRoot / PROJECT_FILE;
    nlohmann::json doc;
    if (!readForWrite(file, doc)) return false;

    doc["name"]          = project.name;
    doc["description"]   = project.description;
    doc["version"]       = project.version;
    doc["engineVersion"] = project.engineVersion;
    doc["entryScene"]    = project.entryScene;
    doc["tickRate"]      = project.tickRate;
    doc["maxPlayers"]    = project.maxPlayers;
    doc["netPort"]       = project.netPort;
    // Written only when named, so a project that never named one keeps a lean file.
    std::vector<std::string> layers = project.physicsLayers;
    while (!layers.empty() && layers.back().empty()) layers.pop_back();
    if (layers.empty()) doc.erase("physicsLayers");
    else                doc["physicsLayers"] = layers;

    doc["render"] = renderJson(project.render);

    // A list as the file already reads is left as written, entries loadProject skips and all.
    if (fontsIn(doc, false) != project.fonts) {
        nlohmann::json fonts = nlohmann::json::array();
        for (const FontEntry& font : project.fonts) {
            fonts.push_back({{"name", font.name}, {"file", font.file}});
        }
        doc["fonts"] = std::move(fonts);
        if (project.fonts.empty()) doc.erase("fonts");
    }
    if (splashIn(doc, false) != project.splash) {
        nlohmann::json splash = nlohmann::json::array();
        for (const SplashEntry& logo : project.splash) {
            splash.push_back({{"image", logo.image}, {"seconds", logo.seconds}});
        }
        doc["splash"] = std::move(splash);
        if (project.splash.empty()) doc.erase("splash");
    }
    // Left as written while the file reads the same, as the lists are; else only what is off the
    // defaults, and no block when nothing is.
    if (loadingIn(doc, false) != project.loading) {
        nlohmann::json loading = nlohmann::json::object();
        if (!project.loading.tips.empty()) loading["tips"] = project.loading.tips;
        if (!project.loading.screen.empty()) loading["screen"] = project.loading.screen;
        if (!project.loading.water) loading["water"] = false;
        doc["loading"] = std::move(loading);
        if (project.loading == LoadingScreen{}) doc.erase("loading");
    }

    return detail::writeJsonFile(file, doc, "project");
}

bool saveProjectRender(const fs::path& projectRoot, const RenderSettings& render) {
    const fs::path file = projectRoot / PROJECT_FILE;
    nlohmann::json doc;
    if (!readForWrite(file, doc)) return false;
    doc["render"] = renderJson(render);
    return detail::writeJsonFile(file, doc, "project");
}

bool saveProjectEngine(const fs::path& projectRoot) {
    const fs::path file = projectRoot / PROJECT_FILE;
    nlohmann::json doc;
    if (!readForWrite(file, doc)) return false;
    doc["engineVersion"] = APP_VERSION;
    return detail::writeJsonFile(file, doc, "project");
}

bool isProjectRoot(const fs::path& directory) {
    std::error_code ec;
    return fs::exists(directory / PROJECT_FILE, ec);
}

bool sameProjectRender(const RenderSettings& a, const RenderSettings& b) {
    return renderJson(a) == renderJson(b);
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
