#include "session/content_pack.h"

#include <algorithm>
#include <system_error>

#include "io/project_paths.h"

namespace Vkm::Engine {
namespace ContentPack {

std::filesystem::path shipped(const std::string& name) {
    return ProjectPaths::engineContent() / name;
}

std::vector<std::string> prefabsIn(const std::filesystem::path& pack, const std::string& folder) {
    std::vector<std::string> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(pack / "prefabs" / folder, ec)) {
        if (entry.path().extension() == ".json") files.push_back(entry.path().filename().string());
    }
    std::sort(files.begin(), files.end());
    return files;
}

int bringInto(const std::filesystem::path& pack, const std::filesystem::path& project, std::string& error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(pack, ec)) {
        error = pack.string() + " is not there";
        return -1;
    }
    int copied = 0;
    std::error_code walked;
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(pack, walked)) {
        if (!entry.is_regular_file(ec)) continue;
        const fs::path target = project / fs::relative(entry.path(), pack, ec);
        if (fs::exists(target, ec)) continue;
        fs::create_directories(target.parent_path(), ec);
        if (ec || !fs::copy_file(entry.path(), target, ec)) {
            error = target.string() + ": " + ec.message();
            return -1;
        }
        ++copied;
    }
    if (walked) {
        error = pack.string() + ": " + walked.message();
        return -1;
    }
    return copied;
}

} // namespace ContentPack
} // namespace Vkm::Engine
