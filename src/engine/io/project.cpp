#define VKM_LOG_CATEGORY "IO"

#include "io/project.h"

#include <fstream>
#include <system_error>

#include <nlohmann/json.hpp>

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

    // The field reads are inside the try as well as the parse: value<std::string>
    // throws on a key that holds a number or an array, which is exactly what a
    // hand-edited project.json produces. A bad file leaves the defaults standing
    // rather than taking the process down.
    nlohmann::json doc;
    try {
        in >> doc;
        out.name          = doc.value("name",          out.name);
        out.engineVersion = doc.value("engineVersion", out.engineVersion);
        out.entryScene    = doc.value("entryScene",    out.entryScene);
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

fs::path findProjectRoot(const fs::path& start) {
    std::error_code ec;

    // Accept a file as well as a directory, so passing a scene finds its project.
    fs::path dir = fs::is_directory(start, ec) ? start : start.parent_path();

    // Normalised before the walk, because the answer gets composed on: it names
    // the log directory and is the string the editor's recent list dedupes by.
    // A path typed "proj/" - what shell completion produces - ends in an empty
    // element, "proj/." in a dot, and both compose away, so "logs/<project>/"
    // silently loses its <project>. Only the path handed in can carry one; the
    // loop below reaches every other directory through parent_path().
    dir = fs::absolute(dir, ec).lexically_normal();
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
