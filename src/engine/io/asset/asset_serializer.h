#pragma once

#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "ecs/entity.h"
#include "io/scene/component_serializer.h"
#include "resource/asset/material_asset.h"
#include "resource/asset_type.h"

namespace Vkm::Engine {

class ResourceManager;
class Scene;

/**
 * @brief Serialize / deserialize the asset graph referenced by a Scene.
 *
 * Saves name every asset the scene references, one section per kind keyed like the library's
 * directories; an unnamed asset has no serializable identity and is skipped. Loads resolve names through
 * the asset library - the cooked file, else the recipe through the AssetFactory seam - skipping names
 * ResourceManager already holds; a name neither serves stays unresolved. Sections load in
 * ASSET_DEPENDENCY_ORDER, so textures land before the materials that resolve them.
 */
namespace AssetSerializer {
    /**
     * @brief Every asset reference a set of entities holds, as an assets block would name it.
     *
     * Handles from components, plus names with no handle: an AssetRef a behavior authors, and a name the
     * last load could not resolve, kept so a save still names it. An asset's own references (a
     * material's textures, a mesh's or a clip's rig) are not here.
     */
    struct EntityAssetRefs {
        ComponentSerializer::AssetRefs                 handles;
        std::vector<std::pair<AssetType, std::string>> names;
    };

    /**
     * @brief Append every asset reference @p id holds to @p refs.
     *
     * The one walk over an entity's references: a field that names an asset is seen here or nowhere.
     *
     * @param scene Scene holding the entity.
     * @param id Entity to walk; alive.
     * @param resources The asset graph the entity's handles index into.
     * @param refs Where the references are appended; duplicates are kept.
     */
    void collectAssetRefs(
        const Scene& scene,
        EntityId id,
        const ResourceManager& resources,
        EntityAssetRefs& refs
    );

    /**
     * @brief The assets block for a chosen set of entities.
     *
     * What a prefab needs: its file lists the assets its subtree names, not the whole scene's.
     *
     * @param scene Scene holding the entities.
     * @param entities The entities to walk; must be alive.
     * @param resources Resolves each handle to the asset it names.
     * @return One array per asset kind, keyed by its VKM_ASSET_KINDS directory name.
     */
    nlohmann::json saveAssetsForEntities(
        const Scene& scene,
        const std::vector<EntityId>& entities,
        const ResourceManager& resources
    );

    /**
     * @brief The assets block for every entity in @p scene.
     *
     * Includes entities inside prefab instances, though the prefab file has its own block: an instance
     * may override a Mesh or a Decal with an asset the prefab never names.
     *
     * @param scene Scene to walk.
     * @param resources Resolves each handle to the asset it names.
     * @return An object with the same section keys loadAssets reads.
     */
    nlohmann::json saveAssetsForScene(const Scene& scene, const ResourceManager& resources);

    /**
     * @brief The assets block for everything @p resources holds, referenced or not.
     *
     * Unlike saveAssetsForScene, also lists assets not yet assigned to anything, which restoring a whole
     * graph needs (see PlaySnapshot). Hidden and unnamed assets are skipped: a private preview asset is
     * not the author's, and loadAssets finds by name.
     *
     * @param resources The asset graph to enumerate.
     * @return An object with the same section keys loadAssets reads.
     */
    nlohmann::json saveAllAssets(const ResourceManager& resources);

    /**
     * @brief What loadAssets does about a name @p resources already holds.
     */
    enum class LoadMode {
        /**
         * @brief Leave it alone.
         *
         * What a load wants: a name not held is built; a held one already is what the document describes.
         */
        Create,
        /**
         * @brief Rebuild a material's contents in place, keeping its handle and name; leave every other
         *        kind alone, as Create does.
         *
         * For restoring a snapshot (see PlaySnapshot). In place, so handles already held still name the
         * material (see ResourceManager::swapValue). Materials alone: their rebuild reads a small recipe,
         * where other kinds would re-read cooked data, so those keep edits made during the session.
         */
        Reload
    };

    /**
     * @brief Recreate the assets a document names into @p resources.
     *
     * @param assetsJson An assets block, from any of the save functions above.
     * @param resources The asset graph to build into.
     * @param mode What to do about a name the graph already holds; see LoadMode.
     * @return False if the block was not an object; per-asset failures are logged and skipped.
     */
    bool loadAssets(
        const nlohmann::json& assetsJson,
        ResourceManager& resources,
        LoadMode mode = LoadMode::Create
    );

    /**
     * @brief Drop every asset in @p resources that @p assetsJson does not name.
     *
     * The other half of a restore: loadAssets leaves alone what the document does not name, and a session
     * that generates its world creates assets the document has never heard of. Only the kinds
     * saveAllAssets writes are considered, so never a font. Hidden assets are kept: save filters them, so
     * their absence says nothing.
     *
     * @param assetsJson A document from saveAllAssets().
     * @param resources Graph to prune.
     * @return How many assets were removed.
     */
    size_t dropAssetsNotIn(const nlohmann::json& assetsJson, ResourceManager& resources);

    /**
     * @brief Apply an "inline" material descriptor (kind=="inline") to a
     *        freshly constructed MaterialAsset.
     *
     * A missing key keeps the field's value; a wrong-typed value throws, reported against the material.
     *
     * @param source    The descriptor, as materialToInline writes it.
     * @param target    The material to fill.
     * @param resources Resolves texture names; one it does not hold leaves its slot as it was.
     */
    void applyInline(const nlohmann::json& source, MaterialAsset& target, const ResourceManager& resources);

    /**
     * @brief Build a material's canonical "inline" source descriptor: PBR scalars
     *        plus texture refs by name.
     *
     * The material's editable source of truth, its runtime form, and what its recipe file holds.
     *
     * @param material  The material to describe.
     * @param resources Resolves its texture handles to the names written.
     * @return The descriptor.
     */
    nlohmann::json materialToInline(const MaterialAsset& material, const ResourceManager& resources);

} // namespace AssetSerializer

} // namespace Vkm::Engine
