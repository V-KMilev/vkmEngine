#define VKM_LOG_CATEGORY "IO"

#include "io/project.h"

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
        LOG_INFO("No %s in '%s'; running an unnamed project",
                 PROJECT_FILE, projectRoot.string().c_str());
        return false;
    }

    std::ifstream in(file);
    if (!in) {
        LOG_ERROR("Cannot open '%s'", file.string().c_str());
        return false;
    }

    // The field reads sit inside the try with the parse: value<std::string>
    // throws on a key holding a number or an array, which is exactly what a
    // hand-edited project.json produces.
    nlohmann::json doc;
    try {
        in >> doc;
        out.name          = doc.value("name",          out.name);
        out.engineVersion = doc.value("engineVersion", out.engineVersion);
        out.entryScene    = doc.value("entryScene",    out.entryScene);
        out.tickRate      = doc.value("tickRate",      out.tickRate);
        out.maxPlayers    = doc.value("maxPlayers",    out.maxPlayers);
        out.netPort       = doc.value("netPort",       out.netPort);

        // Absent fields keep their defaults, so a project.json written before
        // the render block existed opens as the engine's own look rather than
        // as a black screen.
        if (doc.contains("render") && doc["render"].is_object()) {
            const nlohmann::json& render = doc["render"];
            visitShippedRenderFields(out.render, [&](const char* key, auto& field) {
                using Field = std::decay_t<decltype(field)>;
                if (!render.contains(key)) return;
                // An enum with a registered name table is written as its name,
                // so a hand-edited project.json reads as one. One without a
                // table has no names to write and travels as its value.
                if constexpr (std::is_enum_v<Field> && Reflect::HAS_ENUM_NAMES<Field>) {
                    if (render[key].is_string()) {
                        Reflect::enumFromNameChecked(
                            render[key].get<std::string>(), field);
                    }
                } else if constexpr (std::is_enum_v<Field>) {
                    field = static_cast<Field>(
                        render[key].get<std::underlying_type_t<Field>>());
                } else {
                    field = render[key].get<Field>();
                }
            });
        }

        // An entry with no image names nothing to show, so it is skipped rather
        // than becoming a black pause of its own.
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
    } else if (out.engineVersion == APP_VERSION) {
        LOG_INFO("Project '%s' (engine %s)", out.name.c_str(), out.engineVersion.c_str());
    } else {
        // Not fatal: the formats are usually compatible across a minor version,
        // and refusing to open would be worse than opening with a warning.
        LOG_WARNING("Project '%s' was authored against engine %s; this is %s",
                    out.name.c_str(), out.engineVersion.c_str(), APP_VERSION);
    }
    return true;
}

bool saveProject(const fs::path& projectRoot, const Project& project) {
    const fs::path file = projectRoot / PROJECT_FILE;

    // Read first, so a key this build knows nothing about survives being written
    // by it. A missing or malformed file is not a reason to refuse: what comes out
    // is then a document holding exactly what is known.
    std::error_code ec;
    nlohmann::json  doc = nlohmann::json::object();
    if (fs::exists(file, ec) && (!detail::readJsonFile(file, doc, "project") || !doc.is_object())) {
        doc = nlohmann::json::object();
    }

    doc["name"]          = project.name;
    doc["engineVersion"] = project.engineVersion;
    doc["entryScene"]    = project.entryScene;
    doc["tickRate"]      = project.tickRate;
    doc["maxPlayers"]    = project.maxPlayers;
    doc["netPort"]       = project.netPort;

    nlohmann::json render = nlohmann::json::object();
    visitShippedRenderFields(const_cast<RenderSettings&>(project.render),
                             [&](const char* key, auto& field) {
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

fs::path findProjectRoot(const fs::path& start) {
    std::error_code ec;

    // Accept a file as well as a directory, so passing a scene finds its project.
    fs::path dir = fs::is_directory(start, ec) ? start : start.parent_path();

    // A path typed "proj/" - what shell completion produces - ends in an empty
    // element and "proj/." in a dot, both of which compose away, so a
    // "logs/<project>/" built on one would silently lose its <project>.
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
