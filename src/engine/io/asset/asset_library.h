#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/reflect.h"

namespace Engine {

/**
 * @brief The asset kinds that live in the library (shaders stay source-referenced and
 * are not part of the cooked database).
 */
enum class AssetType : uint8_t {
    Mesh,
    Texture,
    Material,

    Count
};

VKM_ENUM_NAMES(AssetType, "mesh", "texture", "material")

struct AssetRecord {
    AssetType   type = AssetType::Mesh;
    std::string name;
    std::string recipeFile;          ///< Relative to ProjectPaths::library().
    std::string cookedFile;          ///< Relative to ProjectPaths::cooked(); empty for materials.
    uint64_t    recipeHash = 0;      ///< hash(recipe + the cooker's COOKER_VERSION); guards staleness.
};

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
        ~AssetLibrary() = default;

        AssetLibrary(const AssetLibrary& other) = delete;
        AssetLibrary& operator=(const AssetLibrary& other) = delete;

        AssetLibrary(AssetLibrary && other) noexcept = delete;
        AssetLibrary& operator=(AssetLibrary && other) noexcept = delete;

    public:
        static AssetLibrary& get();

        /**
         * @brief (Re)load the manifest from disk, replacing current state.
         *
         * A missing manifest is not an error - an empty library is a valid fresh
         * project. One written in a layout version this build does not know is
         * refused the same way: the library is derived data, and re-cooking is
         * cheaper than guessing at what an unknown layout meant.
         */
        void load();

        /**
         * @brief Resolve (type, name) to its record, or nullptr if absent.
         *
         * @param type Asset type half of the lookup key.
         * @param name Asset name half of the lookup key.
         * @return Pointer to the matching record, or nullptr if none is registered.
         */
        const AssetRecord* find(AssetType type, const std::string& name) const;

        /**
         * @brief Every registered asset name of @p type, sorted.
         *
         * The manifest is the authoritative name->record map, so this is how a
         * caller discovers what a project actually has rather than hardcoding
         * names. Sorted because the backing store is unordered: callers that
         * need a reproducible selection (a benchmark picking the same meshes on
         * every run) would otherwise get a different set per process.
         *
         * @param type Asset type to enumerate.
         * @return Sorted names; empty if the type has no registered records.
         */
        std::vector<std::string> namesOf(AssetType type) const;

        /**
         * @brief Resolve a record's recipe file to an absolute path.
         *
         * @param record The library record whose recipe file is wanted.
         * @return Absolute path to the record's recipe file under the project library dir.
         */
        std::filesystem::path recipePath(const AssetRecord& record) const;

        /**
         * @brief Resolve a record's cooked file to an absolute path.
         *
         * @param record The library record whose cooked file is wanted.
         * @return Absolute path to the record's cooked file under the project cooked dir.
         */
        std::filesystem::path cookedPath(const AssetRecord& record) const;

        // Editor-only mutation. upsert replaces any existing record for (type,name).
        void upsert(AssetRecord record);
        void remove(AssetType type, const std::string& name);
        bool save() const;

        /**
         * @brief Stable per-identity UID (16 hex chars of a content hash of type+name),
         * used to name recipe/cooked files.
         */
        static std::string uidFor(AssetType type, const std::string& name);

    private:
        AssetLibrary() = default;

        static std::string key(AssetType type, const std::string& name);

    private:
        std::unordered_map<std::string, AssetRecord> m_records;  ///< keyed by key(type,name)
};

} // namespace Engine
