#include "io/project_paths.h"

#include <system_error>

#if defined(_WIN32)
    // Guarded: the MinGW libstdc++ headers (pulled in via <filesystem>) already
    // define NOMINMAX, so a bare redefine warns.
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <climits>
    #include <unistd.h>
#endif

namespace Engine::ProjectPaths {

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
    // A packaged build ships its data beside the executable - or one level up
    // when the exe lives in a bin/ subfolder. Detect that by a shaders/ folder
    // (always shipped) and root there so the game is relocatable. Dev runs the
    // exe from build/bin with no such folder nearby, and falls back to the
    // build-time repo root.
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
    static std::filesystem::path path;
    return path;
}

} // namespace

std::filesystem::path engineRoot() {
    // Resolved once: the on-disk layout can't change under a running process.
    static const std::filesystem::path resolved = resolveRoot();
    return resolved;
}

void setProjectRoot(const std::filesystem::path& path) {
    std::error_code ec;
    projectOverride() = std::filesystem::absolute(path, ec);
}

std::filesystem::path projectRoot() {
    // An explicit project wins. Without one the project is wherever the engine
    // is, which is what a development checkout and a packaged game both want:
    // the repo is its own project, and a shipped game keeps its data beside the
    // executable.
    if (!projectOverride().empty()) return projectOverride();

    static const std::filesystem::path resolved = resolveRoot();
    return resolved;
}

} // namespace Engine::ProjectPaths
