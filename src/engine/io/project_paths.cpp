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

} // namespace

std::filesystem::path engineRoot() {
    // Resolved once: the on-disk layout can't change under a running process.
    static const std::filesystem::path resolved = resolveRoot();
    return resolved;
}

std::filesystem::path projectRoot() {
    // Still the same directory as the engine root: this version splits the two
    // names apart so every call site declares which it means, and a later step
    // gives the project its own location once project.json can say where it is.
    // Splitting the API first keeps that change from also being a rename of
    // every path in the engine.
    static const std::filesystem::path resolved = resolveRoot();
    return resolved;
}

} // namespace Engine::ProjectPaths
