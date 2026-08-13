#define VKM_LOG_CATEGORY "IO"

#include "io/asset/asset_library.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <system_error>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "core/hash/fnv1a.h"
#include "io/json_file.h"
#include "io/project_paths.h"

namespace Engine {

namespace {

constexpr uint32_t MANIFEST_VERSION = 1;

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

std::filesystem::path AssetLibrary::recipePath(const AssetRecord& record) const {
    return ProjectPaths::library() / record.recipeFile;
}

std::filesystem::path AssetLibrary::cookedPath(const AssetRecord& record) const {
    return ProjectPaths::cooked() / record.cookedFile;
}

void AssetLibrary::load() {
    m_records.clear();
    m_cookerVersion = 0;

    const std::filesystem::path path = ProjectPaths::libraryManifest();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        LOG_INFO("Asset library manifest not found (%s); starting empty", path.string().c_str());
        return;
    }

    nlohmann::json doc;
    if (!detail::readJsonFile(path, doc, "Asset library manifest")) return;

    m_cookerVersion = doc.value("cookerVersion", 0u);

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
        r.recipeFile = entry.value("recipe", std::string{});
        r.cookedFile = entry.value("cooked", std::string{});
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
    doc["cookerVersion"]   = m_cookerVersion;

    nlohmann::json assets = nlohmann::json::array();
    for (const auto& [k, r] : m_records) {
        (void)k;
        nlohmann::json entry;
        entry["name"]   = r.name;
        entry["type"]   = Reflect::enumName(r.type);
        entry["recipe"] = r.recipeFile;
        if (!r.cookedFile.empty()) entry["cooked"] = r.cookedFile;
        entry["hash"]   = r.recipeHash;
        assets.push_back(std::move(entry));
    }
    doc["assets"] = std::move(assets);

    const std::filesystem::path path = ProjectPaths::libraryManifest();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    // Atomic write: serialize to a sibling temp file then rename over the target,
    // mirroring EditorSettings::save so a crash mid-write can't truncate it.
    const std::filesystem::path tmp = std::filesystem::path(path).concat(".tmp");
    {
        std::ofstream out(tmp);
        if (!out) {
            LOG_ERROR("Asset library: cannot open %s for write", tmp.string().c_str());
            return false;
        }
        out << doc.dump(2);
        if (!out) {
            LOG_ERROR("Asset library: write to %s failed", tmp.string().c_str());
            return false;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        LOG_ERROR("Asset library: rename %s -> %s failed: %s",
                  tmp.string().c_str(), path.string().c_str(), ec.message().c_str());
        return false;
    }
    return true;
}

} // namespace Engine
