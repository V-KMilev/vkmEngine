#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "resource/asset_id.h"

namespace Engine {

enum class AssetKind : uint8_t {
    Unknown = 0,
    Texture,
    Material,
    Mesh,
    Shader,
};

const char* toString(AssetKind kind);
AssetKind   assetKindFromString(std::string_view s);

/**
 * @brief Persistent map from AssetId to on-disk path + asset kind.
 *
 * Process-wide singleton. Loaded from assets/_database.json at boot via
 * initFromDisk(); registerOrGet() auto-saves whenever a new entry is
 * inserted, so subsequent runs see the same GUIDs for the same paths.
 *
 * Today this is consulted by loaders to stamp asset.assetId on import.
 * In a later phase scenes will reference assets by GUID instead of path,
 * at which point this becomes the resolver for "I have this GUID, where
 * does it live on disk?"
 */
class AssetDatabase {
    public:
        AssetDatabase(const AssetDatabase& other) = delete;
        AssetDatabase& operator=(const AssetDatabase& other) = delete;

        AssetDatabase(AssetDatabase && other) = delete;
        AssetDatabase& operator=(AssetDatabase && other) = delete;

    public:
        static AssetDatabase& get();

        /**
         * @brief Bind the database to a JSON file on disk.
         *
         * If @p dbPath exists, parses it; otherwise starts empty (first-run
         * is normal). Subsequent registerOrGet() calls write back to the
         * same path. Must be called once at boot before any loader runs.
         */
        void initFromDisk(const std::string& dbPath);

        /**
         * @brief Resolve @p path to an AssetId, generating a fresh GUID
         *        on first sight and persisting it.
         *
         * @p path is used as-is - callers should pass the canonical form
         * they intend to read with. @p kind tags the entry so the JSON
         * file is human-readable; subsequent calls with a more specific
         * kind upgrade an Unknown entry but never overwrite a known one.
         */
        AssetId registerOrGet(const std::string& path, AssetKind kind);

        /**
         * @brief Insert a path/kind pair with an explicit AssetId.
         *
         * Used by scene loading to seed the database from the IDs the
         * scene file recorded - that way assets the scene references by
         * GUID resolve correctly even on a fresh machine whose database
         * was previously empty.
         *
         * If the database already has @p path under a different id, the
         * existing entry wins (no overwrite) and the caller is warned.
         */
        void registerWithId(const std::string& path, AssetId id, AssetKind kind);

        /// Look up the path for @p id. Returns nullptr if unknown.
        const std::string* findPath(AssetId id) const;

        /// Look up the AssetId for @p path. Returns invalid AssetId if unknown.
        AssetId findId(const std::string& path) const;

        size_t size() const { return m_byId.size(); }

    private:
        AssetDatabase() = default;

        struct Record {
            std::string path;
            AssetKind   kind = AssetKind::Unknown;
        };

        void writeToDisk();

        std::unordered_map<AssetId, Record>      m_byId;
        std::unordered_map<std::string, AssetId> m_byPath;
        std::string m_dbPath;
        bool        m_ready = false;
};

} // namespace Engine
