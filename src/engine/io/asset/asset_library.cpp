#define VKM_LOG_CATEGORY "IO"

#include "io/asset/asset_library.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <system_error>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "core/hash/fnv1a.h"
#include "io/json_file.h"
#include "io/project_paths.h"

namespace Engine {

namespace {

constexpr uint32_t MANIFEST_VERSION = 1;

// Directory an asset type keeps its files in, under both library() and cooked().
constexpr const char* TYPE_DIRS[] = {"meshes", "textures", "materials"};
static_assert(sizeof(TYPE_DIRS) / sizeof(TYPE_DIRS[0]) == static_cast<size_t>(AssetType::Count),
              "TYPE_DIRS must stay in sync with AssetType");

} // namespace

AssetLibrary& AssetLibrary::get() {
    static AssetLibrary instance;
    return instance;
}

std::string AssetLibrary::key(AssetType type, const std::string& name) {
    return std::string(Reflect::enumName(type)) + ':' + name;
}

std::string AssetLibrary::uidFor(AssetType type, const std::string& name) {
    const uint64_t h = fnv1a64(key(type, name));
    std::array<char, 17> buf{};
    std::snprintf(buf.data(), buf.size(), "%016llx", static_cast<unsigned long long>(h));
    return std::string(buf.data());
}

std::filesystem::path AssetLibrary::recipePath(AssetType type, const std::string& name) {
    return ProjectPaths::library() / TYPE_DIRS[static_cast<size_t>(type)] / (uidFor(type, name) + ".json");
}

std::filesystem::path AssetLibrary::cookedPath(AssetType type, const std::string& name) {
    return ProjectPaths::cooked() / TYPE_DIRS[static_cast<size_t>(type)] / (uidFor(type, name) + ".vkmc");
}

void AssetLibrary::load() {
    m_records.clear();

    const std::filesystem::path path = ProjectPaths::libraryManifest();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        LOG_INFO("Asset library manifest not found (%s); starting empty", path.string().c_str());
        return;
    }

    nlohmann::json doc;
    if (!detail::readJsonFile(path, doc, "Asset library manifest")) return;

    // Derived data: a manifest this build cannot read is refused outright rather
    // than half-understood, and the recovery is to re-cook the project.
    const uint32_t version = doc.value("manifestVersion", 0u);
    if (version != MANIFEST_VERSION) {
        LOG_ERROR("Asset library: manifest %s is version %u, not %u; starting empty (re-cook the project)",
            path.string().c_str(), version, MANIFEST_VERSION);
        return;
    }

    const auto assets = doc.find("assets");
    if (assets == doc.end() || !assets->is_array()) {
        LOG_WARNING("Asset library: manifest has no assets array");
        return;
    }

    for (const auto& entry : *assets) {
        AssetRecord r;
        const std::string typeStr = entry.value("type", std::string{});
        r.type = Reflect::enumFromName<AssetType>(typeStr);
        if (Reflect::enumName(r.type) != typeStr) {   // unknown tag falls back to value 0
            LOG_WARNING("Asset library: entry with unknown type '%s', skipping", typeStr.c_str());
            continue;
        }
        r.name       = entry.value("name", std::string{});
        r.recipeHash = entry.value("hash", uint64_t{0});
        if (r.name.empty()) {
            LOG_WARNING("Asset library: entry with empty name, skipping");
            continue;
        }
        m_records[key(r.type, r.name)] = std::move(r);
    }

    LOG_INFO("Asset library: loaded %zu record(s) from %s", m_records.size(), path.string().c_str());
}

const AssetRecord* AssetLibrary::find(AssetType type, const std::string& name) const {
    auto it = m_records.find(key(type, name));
    return it == m_records.end() ? nullptr : &it->second;
}

std::vector<std::string> AssetLibrary::namesOf(AssetType type) const {
    std::vector<std::string> names;
    for (const auto& [_, record] : m_records) {
        if (record.type == type) names.push_back(record.name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

void AssetLibrary::upsert(AssetRecord record) {
    const std::string k = key(record.type, record.name);
    m_records[k] = std::move(record);
}

void AssetLibrary::remove(AssetType type, const std::string& name) {
    m_records.erase(key(type, name));
}

bool AssetLibrary::save() const {
    nlohmann::json doc;
    doc["manifestVersion"] = MANIFEST_VERSION;

    nlohmann::json assets = nlohmann::json::array();
    for (const auto& [k, r] : m_records) {
        (void)k;
        nlohmann::json entry;
        entry["name"] = r.name;
        entry["type"] = Reflect::enumName(r.type);
        entry["hash"] = r.recipeHash;
        assets.push_back(std::move(entry));
    }
    doc["assets"] = std::move(assets);

    return detail::writeJsonFile(ProjectPaths::libraryManifest(), doc, "Asset library manifest");
}

} // namespace Engine
