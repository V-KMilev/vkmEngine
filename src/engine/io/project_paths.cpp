#include "io/project_paths.h"

#include <cstdlib>
#include <system_error>

#include "platform/windows_api.h"

#if !defined(_WIN32)
    #include <climits>
    #include <unistd.h>
#endif

namespace Vkm::Engine::ProjectPaths {

namespace {

// Absolute directory of the running executable, or empty on failure.
std::filesystem::path executableDir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    const DWORD len = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return {};  // failed or truncated
    return std::filesystem::path(buf, buf + len).parent_path();
#else
    char buf[PATH_MAX];
    const ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf));
    if (len <= 0 || static_cast<size_t>(len) >= sizeof(buf)) return {};
    return std::filesystem::path(buf, buf + len).parent_path();
#endif
}

std::filesystem::path resolveRoot() {
    // The marker for a packaged layout is the shaders/ folder, which always
    // ships; rooting there is what makes the game relocatable.
    const std::filesystem::path exeDir = executableDir();
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

// Folder the engine keeps its per-user files under, inside whichever directory
// the platform names for them.
constexpr const char* USER_DIR_NAME = "vkmEngine";

// An environment variable as a path, or empty when it is unset or blank. Wide on
// Windows because a user directory contains a user name, and the narrow
// environment mangles any that is not ANSI.
#if defined(_WIN32)
std::filesystem::path envPath(const wchar_t* name) {
    const wchar_t* value = ::_wgetenv(name);
    return (value && *value) ? std::filesystem::path(value) : std::filesystem::path{};
}
#else
std::filesystem::path envPath(const char* name) {
    const char* value = std::getenv(name);
    return (value && *value) ? std::filesystem::path(value) : std::filesystem::path{};
}
#endif

// The platform's directory for per-user configuration, empty if it names none.
std::filesystem::path userConfigBase() {
#if defined(_WIN32)
    return envPath(L"APPDATA");
#else
    const std::filesystem::path xdg = envPath("XDG_CONFIG_HOME");
    if (!xdg.empty()) return xdg;
    const std::filesystem::path home = envPath("HOME");
    return home.empty() ? std::filesystem::path{} : home / ".config";
#endif
}

// The platform's directory for per-user state - logs, caches of work in
// progress - which is a different place from configuration on both platforms.
std::filesystem::path userStateBase() {
#if defined(_WIN32)
    return envPath(L"LOCALAPPDATA");
#else
    const std::filesystem::path xdg = envPath("XDG_STATE_HOME");
    if (!xdg.empty()) return xdg;
    const std::filesystem::path home = envPath("HOME");
    return home.empty() ? std::filesystem::path{} : home / ".local" / "state";
#endif
}

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
    // Without an explicit project the engine root doubles as one: a development
    // checkout is its own project, and a shipped game keeps its data beside the
    // executable.
    if (!projectOverride().empty()) return projectOverride();
    return engineRoot();
}

std::filesystem::path userRoot() {
    // Resolved once, and created with it: the caller that asks for this root is
    // about to write in it, and a root that does not exist is a save that fails
    // at shutdown where nobody is looking.
    static const std::filesystem::path s_resolved = [] {
        const std::filesystem::path base = userConfigBase();
        // No home directory at all - a service account, a stripped container.
        if (base.empty()) return engineRoot();

        const std::filesystem::path dir = base / USER_DIR_NAME;
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return ec ? engineRoot() : dir;
    }();
    return s_resolved;
}

std::filesystem::path userLogs() {
    static const std::filesystem::path s_resolved = [] {
        const std::filesystem::path base = userStateBase();
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
    // Empty means the two share no root at all (different Windows drives); a
    // leading ".." component means the file sits outside the project. The match
    // is on whole components, so a directory named "..cache" is still inside.
    const std::string generic = relative.generic_string();
    if (generic.empty() || generic == ".." || generic.rfind("../", 0) == 0) {
        return given.generic_string();
    }
    return generic;
}

} // namespace Vkm::Engine::ProjectPaths
