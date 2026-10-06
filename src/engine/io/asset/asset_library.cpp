#define VKM_LOG_CATEGORY "IO"

#include "io/asset/asset_library.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "core/fnv1a.h"
#include "io/asset/asset_cook.h"
#include "io/json_file.h"
#include "io/project_paths.h"

namespace Vkm::Engine {

namespace {

constexpr uint32_t MANIFEST_VERSION = 2;

// Directory an asset type keeps its files in, under both library() and cooked(); from the kind list, so
// the directory and the kind are one row.
#define VKM_ASSET_DIR(tag, type, name, dir) dir,
constexpr const char* TYPE_DIRS[] = { VKM_ASSET_KINDS(VKM_ASSET_DIR) };
#undef VKM_ASSET_DIR

} // namespace

AssetLibrary& AssetLibrary::get() {
    static AssetLibrary s_instance;
    return s_instance;
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

std::filesystem::path AssetLibrary::cookedPath(AssetType type, const std::string& name, uint64_t recipeHash) {
    // The uid stays ahead of the key so a person can read the directory: which asset a file belongs to,
    // and which version of it.
    std::array<char, 17> key{};
    std::snprintf(
        key.data(),
        key.size(),
        "%016llx",
        static_cast<unsigned long long>(AssetCook::cacheKey(recipeHash, type))
    );
    return ProjectPaths::cooked() / TYPE_DIRS[static_cast<size_t>(type)]
        / (uidFor(type, name) + "-" + key.data() + ".vkmc");
}

bool AssetLibrary::readRecipe(AssetType type, const std::string& name, nlohmann::json& outSource) {
    const std::filesystem::path path = recipePath(type, name);
    nlohmann::json doc;
    if (!detail::readJsonFile(path, doc, "Asset library recipe")) return false;
    if (!doc.is_object() || !doc.contains("source")) {
        LOG_ERROR("Asset library recipe %s has no 'source'", path.string().c_str());
        return false;
    }
    outSource = std::move(doc["source"]);
    return true;
}

bool AssetLibrary::writeRecipe(AssetType type, const std::string& name, const nlohmann::json& source) {
    nlohmann::json doc;
    doc["name"]   = name;
    doc["source"] = source;
    return detail::writeJsonFile(recipePath(type, name), doc, "Asset library recipe");
}

void AssetLibrary::load(Truth truth) {
    m_records.clear();
    loadManifest();
    if (truth == Truth::Recipes) dropRowsWithoutRecipes();
    adoptUnrecordedRecipes();
}

void AssetLibrary::dropRowsWithoutRecipes() {
    size_t dropped = 0;
    for (auto it = m_records.begin(); it != m_records.end();) {
        std::error_code ec;
        if (std::filesystem::exists(recipePath(it->second.type, it->second.name), ec)) {
            ++it;
            continue;
        }
        it = m_records.erase(it);
        ++dropped;
    }
    // Assets renamed or deleted since the manifest was written; the next cook removes their files.
    if (dropped > 0) LOG_INFO("Asset library: %zu manifest row(s) name no recipe and were dropped", dropped);
}

void AssetLibrary::loadManifest() {
    const std::filesystem::path path = ProjectPaths::assetManifest();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        LOG_INFO("Asset library manifest not found (%s); starting empty", path.string().c_str());
        return;
    }

    nlohmann::json doc;
    if (!detail::readJsonFile(path, doc, "Asset library manifest")) return;

    // Derived data: a manifest this build cannot read is refused, not half-understood; re-cooking
    // recovers. nlohmann throws on a field of the wrong type.
    try {
        const uint32_t version = doc.value("manifestVersion", 0u);
        if (version != MANIFEST_VERSION) {
            LOG_ERROR(
                "Asset library: manifest %s is version %u, not %u; starting empty (re-cook the project)",
                path.string().c_str(),
                version,
                MANIFEST_VERSION
            );
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
            if (!Reflect::enumFromNameChecked(typeStr, r.type)) {
                LOG_WARNING("Asset library: entry with unknown type '%s', skipping", typeStr.c_str());
                continue;
            }
            r.name       = entry.value("name", std::string{});
            r.recipeHash = entry.value("hash", uint64_t{0});
            r.sources    = entry.value("sources", std::vector<std::string>{});
            if (r.name.empty()) {
                LOG_WARNING("Asset library: entry with empty name, skipping");
                continue;
            }
            m_records[key(r.type, r.name)] = std::move(r);
        }
    } catch (const nlohmann::json::exception& e) {
        m_records.clear();
        LOG_ERROR(
            "Asset library: manifest %s does not read (%s); starting empty (re-cook the project)",
            path.string().c_str(),
            e.what()
        );
        return;
    }

    LOG_INFO("Asset library: loaded %zu record(s) from %s", m_records.size(), path.string().c_str());
}

void AssetLibrary::adoptUnrecordedRecipes() {
    size_t adopted = 0;
    for (size_t t = 0; t < std::size(TYPE_DIRS); ++t) {
        const std::filesystem::path dir = ProjectPaths::library() / TYPE_DIRS[t];
        std::error_code ec;
        for (const auto& file : std::filesystem::directory_iterator(dir, ec)) {
            if (file.path().extension() != ".json") continue;

            nlohmann::json doc;
            if (!detail::readJsonFile(file.path(), doc, "Asset library recipe")) continue;
            if (!doc.is_object()) continue;
            const auto name = doc.find("name");
            if (name == doc.end() || !name->is_string()) continue;

            AssetRecord r;
            r.type = static_cast<AssetType>(t);
            r.name = name->get<std::string>();
            // A file not named for its identity was copied in by hand; recipePath would never find it.
            if (r.name.empty() || file.path().stem() != uidFor(r.type, r.name)) {
                LOG_WARNING(
                    "Asset library: %s is not where its name says; skipping",
                    file.path().string().c_str()
                );
                continue;
            }
            // Hash 0 names no cooked file, so the asset loads from its recipe; the next cook records it.
            if (m_records.emplace(key(r.type, r.name), std::move(r)).second) ++adopted;
        }
    }
    if (adopted > 0) LOG_INFO("Asset library: %zu recipe(s) had no manifest row and were adopted", adopted);
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

size_t AssetLibrary::removeUnrecordedCooked() const {
    // Spelled as the walk below spells what it finds: the kind's directory, then the file name.
    std::unordered_set<std::string> recorded;
    for (const auto& [_, record] : m_records) {
        recorded.insert(cookedPath(record.type, record.name, record.recipeHash).string());
    }

    std::vector<std::filesystem::path> orphans;
    for (const char* dir : TYPE_DIRS) {
        std::error_code ec;
        // Stepped by hand: the range-for's error_code covers only the constructor, and a failed step throws.
        const std::filesystem::path folder = ProjectPaths::cooked() / dir;
        for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code statError;
            if (it->is_regular_file(statError) && recorded.count(it->path().string()) == 0) {
                orphans.push_back(it->path());
            }
        }
    }

    size_t removed = 0;
    for (const std::filesystem::path& orphan : orphans) {
        std::error_code ec;
        if (std::filesystem::remove(orphan, ec)) ++removed;
    }
    if (removed > 0) {
        LOG_INFO("Asset library: removed %zu cooked file(s) no record names", removed);
    }
    return removed;
}

bool AssetLibrary::save() const {
    nlohmann::json doc;
    doc["manifestVersion"] = MANIFEST_VERSION;

    // Key order, not bucket order: the unordered_map's order changes between saves, rewriting the
    // manifest whenever anything cooks.
    std::vector<const std::string*> keys;
    keys.reserve(m_records.size());
    for (const auto& record : m_records) keys.push_back(&record.first);
    std::sort(keys.begin(), keys.end(), [](const std::string* a, const std::string* b) { return *a < *b; });

    nlohmann::json assets = nlohmann::json::array();
    for (const std::string* k : keys) {
        const AssetRecord& r = m_records.at(*k);
        nlohmann::json entry;
        entry["name"] = r.name;
        entry["type"] = Reflect::enumName(r.type);
        entry["hash"] = r.recipeHash;
        if (!r.sources.empty()) entry["sources"] = r.sources;
        assets.push_back(std::move(entry));
    }
    doc["assets"] = std::move(assets);

    return detail::writeJsonFile(ProjectPaths::assetManifest(), doc, "Asset library manifest");
}

} // namespace Vkm::Engine
