#define VKM_LOG_CATEGORY "IO"

#include "io/project.h"

#include <algorithm>
#include <fstream>
#include <type_traits>
#include <system_error>

#include <nlohmann/json.hpp>

#include "io/json_file.h"
#include "logger.h"

namespace fs = std::filesystem;

namespace Vkm::Engine {

namespace {

constexpr const char* PROJECT_FILE = "project.json";

} // namespace

bool loadProject(const fs::path& projectRoot, Project& out) {
    const fs::path file = projectRoot / PROJECT_FILE;

    std::error_code ec;
    if (!fs::exists(file, ec)) {
        LOG_INFO("No %s in '%s'; running an unnamed project", PROJECT_FILE, projectRoot.string().c_str());
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
        out.name          = doc.value("name",          out.name);
        out.description   = doc.value("description",   out.description);
        out.version       = doc.value("version",       out.version);
        out.engineVersion = doc.value("engineVersion", out.engineVersion);
        out.entryScene    = doc.value("entryScene",    out.entryScene);
        // Clamped here, not at each reader: the clock would not honour an unclamped value.
        const uint32_t askedTick = doc.value("tickRate", out.tickRate);
        out.tickRate = Config::clampTickRate(askedTick);
        if (out.tickRate != askedTick) {
            LOG_WARNING("Project asks for %u ticks a second; running at %u", askedTick, out.tickRate);
        }
        // Clamped here rather than at each reader, as tickRate is.
        const uint32_t askedSeats = doc.value("maxPlayers", out.maxPlayers);
        out.maxPlayers = Config::clampSeats(askedSeats);
        if (out.maxPlayers != askedSeats) {
            LOG_WARNING("Project asks for %u seats; serving %u", askedSeats, out.maxPlayers);
        }
        out.netPort = doc.value("netPort", out.netPort);

        // Absent fields keep their defaults.
        if (doc.contains("render") && doc["render"].is_object()) {
            const nlohmann::json& render = doc["render"];
            visitShippedRenderFields(out.render, [&](const char* key, auto& field) {
                using Field = std::decay_t<decltype(field)>;
                if (!render.contains(key)) return;
                // An enum with a name table travels as its name, one without as its value.
                if constexpr (std::is_enum_v<Field> && Reflect::HAS_ENUM_NAMES<Field>) {
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
                        if constexpr (std::is_enum_v<Field>) {
                            field = static_cast<Field>(render[key].get<std::underlying_type_t<Field>>());
                        } else {
                            field = render[key].get<Field>();
                        }
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

            // See RenderSettings::MIN_GTAO_RADIUS and RenderSettings::isMsaaSampleCount.
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
            if (!RenderSettings::isMsaaSampleCount(out.render.msaaSamples)) {
                LOG_WARNING(
                    "project.json render.msaaSamples %u is not 1, 2, 4 or 8; using %u",
                    out.render.msaaSamples,
                    RenderSettings{}.msaaSamples
                );
                out.render.msaaSamples = RenderSettings{}.msaaSamples;
            }
        }

        // An entry with no image is skipped rather than shown as a black pause.
        if (doc.contains("splash") && doc["splash"].is_array()) {
            for (const nlohmann::json& entry : doc["splash"]) {
                SplashEntry splash;
                splash.image   = entry.value("image", std::string{});
                splash.seconds = entry.value("seconds", splash.seconds);
                if (!splash.image.empty()) out.splash.push_back(std::move(splash));
            }
        }
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

bool saveProject(const fs::path& projectRoot, const Project& project) {
    const fs::path file = projectRoot / PROJECT_FILE;

    // Read first so unknown keys survive. A missing or malformed file starts empty.
    std::error_code ec;
    nlohmann::json  doc = nlohmann::json::object();
    if (fs::exists(file, ec) && (!detail::readJsonFile(file, doc, "project") || !doc.is_object())) {
        doc = nlohmann::json::object();
    }

    doc["name"]          = project.name;
    doc["description"]   = project.description;
    doc["version"]       = project.version;
    doc["engineVersion"] = project.engineVersion;
    doc["entryScene"]    = project.entryScene;
    doc["tickRate"]      = project.tickRate;
    doc["maxPlayers"]    = project.maxPlayers;
    doc["netPort"]       = project.netPort;

    nlohmann::json render = nlohmann::json::object();
    visitShippedRenderFields(project.render, [&](const char* key, auto& field) {
        using Field = std::decay_t<decltype(field)>;
        if constexpr (std::is_enum_v<Field> && Reflect::HAS_ENUM_NAMES<Field>) {
            render[key] = Reflect::enumName(field);
        } else if constexpr (std::is_enum_v<Field>) {
            render[key] = static_cast<std::underlying_type_t<Field>>(field);
        } else {
            render[key] = field;
        }
    });
    doc["render"] = std::move(render);

    return detail::writeJsonFile(file, doc, "project");
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

    fs::path dir = fs::is_directory(start, ec) ? start : start.parent_path();

    // "proj/" ends in an empty element and "proj/." in a dot; both compose away,
    // so "logs/<project>/" built on one would lose its <project>.
    dir = fs::absolute(dir, ec).lexically_normal();
    // Only the path handed in can end that way; the loop reaches every other
    // directory through parent_path().
    if (!dir.has_filename()) dir = dir.parent_path();

    while (!dir.empty()) {
        if (fs::exists(dir / PROJECT_FILE, ec)) return dir;
        const fs::path parent = dir.parent_path();
        if (parent == dir) break;  // reached the filesystem root
        dir = parent;
    }
    return {};
}

} // namespace Vkm::Engine
