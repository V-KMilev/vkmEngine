#include "cook/cook_key.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "core/fnv1a.h"
#include "import/import_formats.h"
#include "import/model_source.h"
#include "io/asset/asset_library.h"
#include "io/project_paths.h"

namespace Vkm::Engine::AssetCooker {

namespace {

// sourceFiles for a path on this machine, the source first.
std::vector<std::filesystem::path> sourceFilesAt(const std::filesystem::path& path) {
    if (path.empty()) return {};
    // A model reads what it names beside it, as its parse finds it.
    const std::string extension = lowercaseExtension(path);
    const auto        models    = MODEL_EXTENSIONS;
    if (std::find(models.begin(), models.end(), extension) != models.end()) return filesReadBy(path);
    return {path};
}

} // namespace

std::vector<std::string> sourceFiles(const std::string& projectRelativePath) {
    std::vector<std::string> files;
    if (projectRelativePath.empty()) return files;
    files.push_back(projectRelativePath);

    const std::vector<std::filesystem::path> onDisk =
        sourceFilesAt(ProjectPaths::resolveProjectPath(projectRelativePath));
    for (size_t i = 1; i < onDisk.size(); ++i) {
        files.push_back(ProjectPaths::toProjectRelative(onDisk[i].string()));
    }
    return files;
}

uint64_t foldSourceContent(const std::string& projectRelativePath, uint64_t seed) {
    if (projectRelativePath.empty()) return seed;

    std::ifstream in(ProjectPaths::resolveProjectPath(projectRelativePath), std::ios::binary);
    if (!in) return fnv1a64("vkm:source-unreadable", seed);

    // Streamed: the largest thing a project imports is what this most needs to hash.
    char     buffer[64 * 1024];
    uint64_t hash = seed;
    while (in.read(buffer, sizeof(buffer)) || in.gcount() > 0) {
        hash = fnv1a64Bytes(buffer, static_cast<std::size_t>(in.gcount()), hash);
    }
    return hash;
}

uint64_t foldDependency(AssetType type, const std::string& name, uint64_t seed) {
    if (name.empty()) return seed;

    const AssetRecord* record = AssetLibrary::get().find(type, name);
    if (!record) return fnv1a64("vkm:dependency-unrecorded", seed);
    return fnv1a64Bytes(&record->recipeHash, sizeof(record->recipeHash), seed);
}

} // namespace Vkm::Engine::AssetCooker
