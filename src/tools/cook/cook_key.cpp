#include "cook/cook_key.h"

#include <algorithm>
#include <cctype>
#include <fstream>

#include <nlohmann/json.hpp>

#include "core/fnv1a.h"
#include "io/asset/asset_library.h"
#include "io/project_paths.h"

namespace Vkm::Engine::AssetCooker {

namespace {

// A glTF names the files beside it by URI: percent-escaped, relative to itself, or not a file (`data:`).
std::string decodeUri(const std::string& uri) {
    std::string out;
    out.reserve(uri.size());
    for (size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size() && std::isxdigit(static_cast<unsigned char>(uri[i + 1]))
            && std::isxdigit(static_cast<unsigned char>(uri[i + 2]))) {
            out.push_back(static_cast<char>(std::stoi(uri.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        } else {
            out.push_back(uri[i]);
        }
    }
    return out;
}

// The buffers the .gltf at @p resolved keeps in files beside it.
void appendGltfBuffers(const std::filesystem::path& resolved, std::vector<std::filesystem::path>& files) {
    nlohmann::json document;
    {
        std::ifstream in(resolved);
        if (!in) return;
        document = nlohmann::json::parse(in, nullptr, false);
    }
    if (document.is_discarded() || !document.is_object()) return;
    const auto buffers = document.find("buffers");
    if (buffers == document.end() || !buffers->is_array()) return;

    for (const nlohmann::json& buffer : *buffers) {
        // A hand-edited file can hold anything here: value() throws on a non-string uri; find() answers
        // end() for a buffer not an object.
        const auto uriNode = buffer.find("uri");
        if (uriNode == buffer.end() || !uriNode->is_string()) continue;
        const std::string uri = uriNode->get<std::string>();
        if (uri.empty() || uri.rfind("data:", 0) == 0) continue;
        files.push_back((resolved.parent_path() / decodeUri(uri)).lexically_normal());
    }
}

// The material libraries the .obj at @p resolved reads: each `mtllib` it names, and the `<name>.mtl`
// beside it the importer also looks for.
void appendObjMaterials(const std::filesystem::path& resolved, std::vector<std::filesystem::path>& files) {
    std::ifstream in(resolved);
    if (!in) return;

    const auto add = [&](const std::filesystem::path& file) {
        const std::filesystem::path normal = file.lexically_normal();
        if (std::find(files.begin(), files.end(), normal) == files.end()) files.push_back(normal);
    };

    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("mtllib", 0) != 0 || line.size() < 7
            || !std::isspace(static_cast<unsigned char>(line[6]))) continue;
        const size_t first = line.find_first_not_of(" \t", 6);
        const size_t last  = line.find_last_not_of(" \t\r");
        if (first == std::string::npos || last < first) continue;
        add(resolved.parent_path() / line.substr(first, last - first + 1));
    }

    std::filesystem::path byName = resolved;
    byName.replace_extension(".mtl");
    std::error_code ec;
    if (std::filesystem::exists(byName, ec)) add(byName);
}

} // namespace

std::vector<std::filesystem::path> sourceFilesAt(const std::filesystem::path& path) {
    std::vector<std::filesystem::path> files;
    if (path.empty()) return files;
    files.push_back(path);

    std::string extension = path.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); }
    );
    if (extension == ".gltf") appendGltfBuffers(path, files);
    if (extension == ".obj")  appendObjMaterials(path, files);
    return files;
}

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
