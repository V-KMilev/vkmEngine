#include "session/scene_recovery.h"

#include <cstdio>
#include <system_error>

#include "core/fnv1a.h"

namespace Vkm::Engine::SceneRecovery {

namespace fs = std::filesystem;

fs::path pathFor(const fs::path& dir, const fs::path& projectRoot, const std::string& scenePath) {
    std::error_code ec;
    const fs::path named = scenePath.empty() ? projectRoot / "(untitled)" : fs::path(scenePath);
    const fs::path whole = fs::absolute(named, ec).lexically_normal();
    const unsigned long long hash = fnv1a64(whole.generic_string());
    char file[32];
    std::snprintf(file, sizeof(file), "%016llx.json", hash);
    return dir / file;
}

fs::path terrainsOf(const fs::path& copy) {
    return fs::path(copy).concat(".terrains");
}

bool hasNewer(const fs::path& copy, const std::string& scenePath) {
    std::error_code ec;
    if (!fs::exists(copy, ec)) return false;
    if (scenePath.empty() || !fs::exists(scenePath, ec)) return true;
    const fs::file_time_type copied = fs::last_write_time(copy, ec);
    if (ec) return false;
    const fs::file_time_type saved = fs::last_write_time(scenePath, ec);
    return ec || copied > saved;
}

void discard(const fs::path& copy) {
    std::error_code ec;
    fs::remove(copy, ec);
    fs::remove_all(terrainsOf(copy), ec);
}

} // namespace Vkm::Engine::SceneRecovery
