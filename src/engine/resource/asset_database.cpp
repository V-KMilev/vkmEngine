#define VKM_LOG_CATEGORY "ASSET_DB"

#include "resource/asset_database.h"

#include <fstream>
#include <utility>

#include <nlohmann/json.hpp>

#include "logger.h"

namespace Engine {

const char* toString(AssetKind kind) {
    switch (kind) {
        case AssetKind::Texture:  return "texture";
        case AssetKind::Material: return "material";
        case AssetKind::Mesh:     return "mesh";
        case AssetKind::Shader:   return "shader";
        case AssetKind::Unknown:
        default:                  return "unknown";
    }
}

AssetKind assetKindFromString(std::string_view s) {
    if (s == "texture")  return AssetKind::Texture;
    if (s == "material") return AssetKind::Material;
    if (s == "mesh")     return AssetKind::Mesh;
    if (s == "shader")   return AssetKind::Shader;
    return AssetKind::Unknown;
}

AssetDatabase& AssetDatabase::get() {
    static AssetDatabase instance;
    return instance;
}

void AssetDatabase::initFromDisk(const std::string& dbPath) {
    m_dbPath = dbPath;
    m_ready  = true;

    std::ifstream in(dbPath);
    if (!in) {
        LOG_INFO("'%s' not found - starting with empty database", dbPath.c_str());
        return;
    }

    nlohmann::json doc;
    try {
        in >> doc;
    } catch (const std::exception& e) {
        LOG_ERROR("parse error in '%s': %s (starting empty)", dbPath.c_str(), e.what());
        return;
    }

    if (!doc.is_object() || !doc.contains("assets") || !doc["assets"].is_array()) {
        LOG_WARNING("'%s' missing 'assets' array (starting empty)", dbPath.c_str());
        return;
    }

    for (const auto& entry : doc["assets"]) {
        if (!entry.contains("id") || !entry.contains("path")) continue;
        AssetId id = AssetId::fromString(entry["id"].get<std::string>());
        if (!id) continue;
        Record r;
        r.path = entry["path"].get<std::string>();
        r.kind = assetKindFromString(entry.value("kind", std::string{"unknown"}));
        m_byPath[r.path] = id;
        m_byId.emplace(id, std::move(r));
    }
    LOG_INFO("Loaded %zu entries from '%s'", m_byId.size(), dbPath.c_str());
}

AssetId AssetDatabase::registerOrGet(const std::string& path, AssetKind kind) {
    if (auto it = m_byPath.find(path); it != m_byPath.end()) {
        // Existing entry. Upgrade Unknown -> known if the caller supplied one;
        // never overwrite a more specific kind once recorded.
        auto& rec = m_byId.at(it->second);
        if (rec.kind == AssetKind::Unknown && kind != AssetKind::Unknown) {
            rec.kind = kind;
            if (m_ready) writeToDisk();
        }
        return it->second;
    }

    AssetId id = AssetId::generate();
    // Re-roll on the astronomically unlikely collision with an existing GUID.
    while (m_byId.count(id) != 0) id = AssetId::generate();

    Record r;
    r.path = path;
    r.kind = kind;
    m_byPath[path] = id;
    m_byId.emplace(id, std::move(r));
    if (m_ready) writeToDisk();
    return id;
}

void AssetDatabase::registerWithId(const std::string& path, AssetId id, AssetKind kind) {
    if (!id) {
        LOG_WARNING("registerWithId('%s') called with invalid AssetId - ignored", path.c_str());
        return;
    }
    if (auto it = m_byPath.find(path); it != m_byPath.end()) {
        if (it->second != id) {
            LOG_WARNING("'%s' already mapped to %s; keeping existing (refused to overwrite with %s)",
                path.c_str(), it->second.toString().c_str(), id.toString().c_str());
        }
        return;
    }
    if (auto it = m_byId.find(id); it != m_byId.end()) {
        LOG_WARNING("AssetId %s already in DB under path '%s'; refusing to re-bind to '%s'",
            id.toString().c_str(), it->second.path.c_str(), path.c_str());
        return;
    }
    Record r;
    r.path = path;
    r.kind = kind;
    m_byPath[path] = id;
    m_byId.emplace(id, std::move(r));
    if (m_ready) writeToDisk();
}

const std::string* AssetDatabase::findPath(AssetId id) const {
    auto it = m_byId.find(id);
    return (it != m_byId.end()) ? &it->second.path : nullptr;
}

AssetId AssetDatabase::findId(const std::string& path) const {
    auto it = m_byPath.find(path);
    return (it != m_byPath.end()) ? it->second : AssetId{};
}

void AssetDatabase::writeToDisk() {
    nlohmann::json doc;
    doc["version"] = 1;
    auto& assets = doc["assets"];
    assets = nlohmann::json::array();
    for (const auto& [id, rec] : m_byId) {
        nlohmann::json e;
        e["id"]   = id.toString();
        e["path"] = rec.path;
        e["kind"] = toString(rec.kind);
        assets.push_back(std::move(e));
    }

    std::ofstream out(m_dbPath);
    if (!out) {
        LOG_ERROR("Failed to open '%s' for writing", m_dbPath.c_str());
        return;
    }
    out << doc.dump(2);
}

} // namespace Engine
