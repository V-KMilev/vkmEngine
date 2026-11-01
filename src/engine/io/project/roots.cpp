#include "io/project/roots.h"

#include <string>
#include <system_error>

#include "io/project/project.h"
#include "vkm_platform/process/dynamic_library.h"
#include "vkm_platform/process/this_process.h"

namespace Vkm::Engine::Roots {

namespace {

std::filesystem::path resolveRoot() {
    // shaders/ always ships, so it marks a packaged (relocatable) layout.
    const std::filesystem::path exeDir = Platform::executableDir();
    if (!exeDir.empty()) {
        std::error_code ec;
        for (const std::filesystem::path& dir : { exeDir, exeDir.parent_path() }) {
            if (std::filesystem::exists(dir / "shaders", ec)) return dir;
        }
    }
    return std::filesystem::path(APP_ROOT_DIR);
}

// Set by setProjectRoot(); empty until then.
std::filesystem::path& projectOverride() {
    static std::filesystem::path s_path;
    return s_path;
}

constexpr const char* USER_DIR_NAME = "vkmEngine";

} // namespace

std::filesystem::path engineRoot() {
    // Resolved once: the on-disk layout can't change under a running process.
    static const std::filesystem::path s_resolved = resolveRoot();
    return s_resolved;
}

void setProjectRoot(const std::filesystem::path& path) {
    std::error_code ec;
    projectOverride() = std::filesystem::absolute(path, ec);
}

std::filesystem::path projectRoot() {
    if (!projectOverride().empty()) return projectOverride();
    return engineRoot();
}

std::filesystem::path userRoot() {
    // Created on resolve: a missing root is a save that fails silently at shutdown.
    static const std::filesystem::path s_resolved = [] {
        const std::filesystem::path base = Platform::userConfigDir();
        if (base.empty()) return engineRoot();

        const std::filesystem::path dir = base / USER_DIR_NAME;
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return ec ? engineRoot() : dir;
    }();
    return s_resolved;
}

std::filesystem::path playerRoot(const std::string& game) {
    // The name's last part, a trailing separator or not, so the files never land loose in the base.
    std::filesystem::path name = std::filesystem::path(game).lexically_normal();
    if (name.filename().empty()) name = name.parent_path();
    name = name.filename();
    const std::filesystem::path base = Platform::userConfigDir();
    if (base.empty() || name.empty() || name == "." || name == "..") return projectRoot();

    // Not made here: the first file written makes it (WholeFileWrite), so a game that saves
    // nothing leaves no folder behind.
    return base / name;
}

std::filesystem::path playerFile(std::string_view name) {
    Project game;
    loadProject(projectRoot(), game);
    return playerRoot(game.name) / std::filesystem::path(name);
}

std::filesystem::path userLogs() {
    static const std::filesystem::path s_resolved = [] {
        const std::filesystem::path base = Platform::userStateDir();
        if (base.empty()) return engineRoot() / "logs";
        return base / USER_DIR_NAME / "logs";
    }();
    return s_resolved;
}

std::filesystem::path resolveProjectPath(const std::string& path) {
    const std::filesystem::path given(path);
    if (given.is_absolute()) return given;
    return (projectRoot() / given).lexically_normal();
}

std::string toProjectRelative(const std::string& path) {
    const std::filesystem::path given = std::filesystem::path(path).lexically_normal();
    if (given.is_relative()) return given.generic_string();

    const std::filesystem::path relative = given.lexically_relative(projectRoot());
    // Empty: no shared root (different drives). A leading ".." component: outside.
    // Matched on whole components, so "..cache" is still inside.
    const std::string generic = relative.generic_string();
    if (generic.empty() || generic == ".." || generic.rfind("../", 0) == 0) {
        return given.generic_string();
    }
    return generic;
}

std::filesystem::path gameplayModule() {
    const std::string file = Platform::DynamicLibrary::platformName("game");
#if defined(VKM_TREE_EXAMPLE_MODULES)
    // A tree but build/ keeps its examples' modules beside its hosts (CMakeLists.txt), so an example
    // runs the one this tree built, never another tree's.
    std::filesystem::path root = projectRoot().lexically_normal();
    if (!root.has_filename()) root = root.parent_path();
    std::error_code ec;
    if (std::filesystem::equivalent(root.parent_path(), engineRoot() / "examples", ec)) {
        return Platform::executableDir() / "examples" / root.filename() / file;
    }
#endif
    return projectBin() / file;
}

} // namespace Vkm::Engine::Roots
