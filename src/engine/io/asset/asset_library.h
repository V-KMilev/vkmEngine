#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "resource/asset_type.h"

namespace Vkm::Engine {

/**
 * @brief One row of the manifest: an asset's identity, and what it was cooked from.
 *
 * The identity is (type, name) alone; where its files sit is derived from it, so a record names no paths.
 */
struct AssetRecord {
    AssetType   type = AssetType::Mesh;
    std::string name;
    /**
     * @brief What the cooked file was last baked from: the recipe, its source art's bytes, and the
     *        recorded key of any asset it derives from.
     *
     * Not the cooker or the layout: AssetCook::cacheKey adds those when it names the file.
     */
    uint64_t    recipeHash = 0;
    /**
     * @brief The source-art files the cook read and folded into recipeHash, as project references
     *        (AssetCooker::sourceFiles).
     *
     * Recorded so the cook that hashes them is their one owner (see import_sources in `tools/vkm`).
     */
    std::vector<std::string> sources;
};

/**
 * @brief In-memory view of the on-disk asset database manifest.
 *
 * The manifest (`ProjectPaths::assetManifest()`) holds every asset's identity (type + name) and recipe
 * hash; scene asset references resolve through it. File locations derive from the identity:
 * recipePath() / cookedPath() use an opaque UID (a content hash of type+name), since a raw asset name
 * may contain path separators or colons.
 */
class AssetLibrary {
    public:
        /**
         * @brief Which file says an asset exists, as load() reads a project.
         *
         * A project's recipes; in a package, which ships only materials' recipes, the manifest.
         */
        enum class Truth {
            Recipes,  ///< A host that imports and cooks: a row whose recipe is gone is dropped.
            Manifest  ///< A host that reads only cooked assets: every row stands.
        };

    public:
        ~AssetLibrary() = default;

        AssetLibrary(const AssetLibrary& other) = delete;
        AssetLibrary& operator=(const AssetLibrary& other) = delete;

        AssetLibrary(AssetLibrary && other) = delete;
        AssetLibrary& operator=(AssetLibrary && other) = delete;

    public:
        static AssetLibrary& get();

        /**
         * @brief (Re)load the library from disk, replacing current state.
         *
         * A recipe with no manifest row is adopted with no cook recorded; under Truth::Recipes a row
         * whose recipe is gone is dropped. A missing manifest is not an error, and one in an unknown
         * layout version is read as missing: re-cooking is cheaper than guessing.
         *
         * @param truth Which file says an asset exists, for the host loading it.
         */
        void load(Truth truth);

        /**
         * @brief Resolve (type, name) to its record, or nullptr if absent.
         *
         * @param type Asset type half of the lookup key.
         * @param name Asset name half of the lookup key.
         * @return The matching record, or nullptr.
         */
        const AssetRecord* find(AssetType type, const std::string& name) const;

        /**
         * @brief Every registered asset name of @p type, sorted.
         *
         * Sorted so a selection is reproducible across processes; the backing store is unordered.
         *
         * @param type Asset type to enumerate.
         * @return Sorted names; empty if the type has no registered records.
         */
        std::vector<std::string> namesOf(AssetType type) const;

        /**
         * @brief Absolute path to the recipe file of (@p type, @p name).
         *
         * Derived from the identity, so writer and readers cannot disagree about where it is.
         *
         * @param type Asset type half of the identity.
         * @param name Asset name half of the identity.
         * @return A path under the project library dir.
         */
        static std::filesystem::path recipePath(AssetType type, const std::string& name);

        /**
         * @brief Absolute path to the cooked binary of (@p type, @p name).
         *
         * Nothing is written here for a Material, whose recipe is its canonical form. The cache key is
         * part of the name, so an artifact baked from a since-changed recipe is not stale, just never
         * asked for.
         *
         * @param type Asset type half of the subject's identity.
         * @param name Asset name half of the subject's identity.
         * @param recipeHash The recorded recipe hash (AssetRecord::recipeHash); AssetCook::cacheKey mixes
         *        in the kind and the cooker version here, so no caller can name a file without them.
         * @return A path under the project cooked dir.
         */
        static std::filesystem::path cookedPath(AssetType type, const std::string& name, uint64_t recipeHash);

        /**
         * @brief Read the `source` object out of the recipe file for (@p type, @p name).
         *
         * A material's recipe is its runtime form; any other kind's is the import to re-run when the
         * cooked cache cannot serve it.
         *
         * @param type Asset type half of the identity.
         * @param name Asset name half of the identity.
         * @param outSource Receives the recipe; untouched on failure.
         * @return False, having logged, when the file is missing, malformed or has no source.
         */
        static bool readRecipe(AssetType type, const std::string& name, nlohmann::json& outSource);

        /**
         * @brief Write @p source as the recipe file for (@p type, @p name).
         *
         * Written to a temporary and renamed over the file: a cook cannot regenerate a recipe, and one
         * truncated in place would be recorded current and refused by every later load.
         *
         * @param type Asset type half of the identity.
         * @param name Asset name half of the identity.
         * @param source The recipe the asset was imported or built from.
         * @return False, having logged, when the file could not be written.
         */
        [[nodiscard]] static bool writeRecipe(
            AssetType type,
            const std::string& name,
            const nlohmann::json& source
        );

        /**
         * @brief Record @p record, replacing any record for its (type, name).
         *
         * In memory until save().
         *
         * @param record Identity and what it was cooked from.
         */
        void upsert(AssetRecord record);

        /**
         * @brief Forget the record for (@p type, @p name), in memory until save().
         *
         * @param type Asset type half of the identity.
         * @param name Asset name half of the identity.
         */
        void remove(AssetType type, const std::string& name);

        /**
         * @brief Write the manifest, every record in key order.
         *
         * @return False, having logged, when the file could not be written.
         */
        [[nodiscard]] bool save() const;

        /**
         * @brief Delete every file in a kind's cooked directory that no record names.
         *
         * Catches what no record can name: files under an older COOKER_VERSION, and
         * temporaries of a killed cook. Only for the host that is the one cooking into the project: an
         * editor baking in the background lands artifacts this manifest may not record yet.
         *
         * @return How many files were deleted.
         */
        size_t removeUnrecordedCooked() const;

    private:
        AssetLibrary() = default;

        static std::string key(AssetType type, const std::string& name);

        /**
         * @brief Read the manifest's rows into the library, if it has a readable one.
         */
        void loadManifest();

        /**
         * @brief Drop every row whose recipe is gone; see Truth::Recipes.
         */
        void dropRowsWithoutRecipes();

        /**
         * @brief Add a row, with no cook recorded, for every recipe that has none.
         */
        void adoptUnrecordedRecipes();

        /**
         * @brief Stable per-identity UID (16 hex chars of a content hash of type+name) naming its files.
         *
         * @param type Asset type half of the identity.
         * @param name Asset name half of the identity.
         * @return The UID, the same on every machine.
         */
        static std::string uidFor(AssetType type, const std::string& name);

    private:
        std::unordered_map<std::string, AssetRecord> m_records;  ///< Keyed by key(type, name).
};

} // namespace Vkm::Engine
