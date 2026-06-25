#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace Engine {

/// The asset kinds that live in the library (shaders stay source-referenced and
/// are not part of the cooked database).
enum class AssetType : uint8_t { Mesh, Texture, Material };

/**
 * @brief In-memory view of the on-disk asset database manifest.
 *
 * The manifest (`ProjectPaths::libraryManifest()`) is the index that maps an
 * asset's name to its recipe file (the editable source of truth) and, for
 * meshes/textures, its cooked binary file. The runtime resolves scene asset
 * references through this; the editor additionally mutates it as assets are
 * created/imported/edited and rewrites it on save.
 *
 * Filenames are opaque UIDs (a content hash of type+name) rather than the raw
 * asset name, which may contain path separators or colons. The manifest is the
 * authoritative name->record map; filenames are an implementation detail.
 */
class AssetLibrary {
    public:
        struct Record {
            AssetType   type = AssetType::Mesh;
            std::string name;
            std::string recipeFile;          ///< Relative to ProjectPaths::library().
            std::string cookedFile;          ///< Relative to ProjectPaths::cooked(); empty for materials.
            uint64_t    recipeHash = 0;      ///< hash(recipe + cookerVersion); guards staleness.
        };

        static AssetLibrary& get();

        /// (Re)load the manifest from disk, replacing current state. A missing
        /// manifest is not an error - an empty library is a valid fresh project.
        void load();

        /// Resolve (type, name) to its record, or nullptr if absent.
        const Record* find(AssetType type, const std::string& name) const;

        /// Absolute paths for a record's files.
        std::filesystem::path recipePath(const Record& record) const;
        std::filesystem::path cookedPath(const Record& record) const;

        // Editor-only mutation. upsert replaces any existing record for (type,name).
        void upsert(Record record);
        void remove(AssetType type, const std::string& name);
        bool save() const;

        /// Stable per-identity UID (16 hex chars of a content hash of type+name),
        /// used to name recipe/cooked files.
        static std::string uidFor(AssetType type, const std::string& name);
        static const char* typeTag(AssetType type);

    private:
        AssetLibrary() = default;

        static std::string key(AssetType type, const std::string& name);

        std::unordered_map<std::string, Record> m_records;  ///< keyed by key(type,name)
        uint32_t m_cookerVersion = 0;                       ///< cooker version recorded in the manifest
};

} // namespace Engine
