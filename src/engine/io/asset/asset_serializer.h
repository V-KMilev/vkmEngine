#pragma once

#include <vector>

#include <nlohmann/json.hpp>

#include "ecs/entity.h"
#include "resource/asset/material_asset.h"

namespace Vkm::Engine {

class ResourceManager;
class Scene;

/**
 * @brief Serialize / deserialize the asset graph referenced by a Scene.
 *
 * Saves emit the meshes, materials, and textures actually referenced by the
 * scene as name-only references; an unnamed asset has no serializable identity
 * and is skipped. Loads resolve each name through the asset library and
 * recreate the asset through the AssetFactory seam (io/asset/asset_factory.h);
 * assets already in ResourceManager (by name) are skipped (idempotent
 * re-loads). A name the cooker never wrote to the library cannot be recreated,
 * so the reference it backs is left unresolved.
 *
 * Textures are their own top-level section: every map a material references is
 * emitted there, and recreated before materials resolve their refs.
 */
namespace AssetSerializer {
    /**
     * @brief The assets block for a chosen set of entities.
     *
     * What a prefab needs: its file describes a subtree, so it lists the assets
     * that subtree names rather than the whole scene's.
     *
     * @param scene Scene holding the entities.
     * @param entities The entities to walk; ids that are not alive are the
     *                 caller's error.
     * @param resources Resolves each handle to the asset it names.
     * @return An object with "textures", "meshes" and "materials" arrays.
     */
    nlohmann::json saveAssetsForEntities(const Scene& scene, const std::vector<EntityId>& entities,
                                         const ResourceManager& resources);

    /**
     * @brief The assets block for every entity in @p scene.
     *
     * Including the entities inside prefab instances, which the scene file does
     * not describe and the prefab file carries its own block for. They are
     * walked anyway because an instance may override a Mesh or a Decal at an
     * asset the prefab never names, and this is the only walk that sees it.
     *
     * @param scene Scene to walk.
     * @param resources Resolves each handle to the asset it names.
     * @return An object with the same section keys loadAssets reads.
     */
    nlohmann::json saveAssetsForScene(const Scene& scene, const ResourceManager& resources);

    /**
     * @brief The assets block for everything @p resources holds, referenced or not.
     *
     * The counterpart to saveAssetsForScene, and the difference is the whole
     * point of it. A scene file lists what the scene names, because that is
     * what a file is for: an asset nothing points at has no business being
     * written into somebody's document. A session is not a file. An asset
     * imported and not yet assigned to anything is in the Asset Browser and in
     * every picker, and it is work somebody did - so the editor's play
     * snapshot, which promises to put the session back exactly as it found it,
     * needs the list a scene save deliberately leaves out.
     *
     * Hidden and unnamed assets are skipped by the same rule the scene save
     * follows: a private preview asset is not the author's, and a name is what
     * loadAssets has to find it by.
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
         * What a load wants: the graph either does not hold the name (and it is
         * built) or holds an asset that already is what the document describes.
         */
        Create,
        /**
         * @brief Rebuild its contents in place, keeping its handle and name.
         *
         * What the editor's Stop wants. A session can edit an asset - the
         * Material Editor stays live in play mode, and a behavior can write
         * through the graph - and Stop puts the world back as Play found it,
         * assets included. Rebuilding in place rather than as a replacement is
         * what lets the undo history survive that: its steps hold the assets
         * they are to put back, and a handle reissued out of a fresh graph
         * names whatever landed in that slot instead.
         */
        Reload
    };

    /**
     * @brief Recreate the assets a document names into @p resources.
     *
     * @param assetsJson An assets block, from any of the save functions above.
     * @param resources The asset graph to build into.
     * @param mode What to do about a name the graph already holds; see LoadMode.
     * @return false if the block was not an object; true otherwise, with
     *         per-asset failures logged and skipped.
     */
    bool loadAssets(const nlohmann::json& assetsJson, ResourceManager& resources,
                    LoadMode mode = LoadMode::Create);

    /**
     * @brief Apply an "inline" material descriptor (kind=="inline") to a freshly-
     * constructed MaterialAsset.
     */
    void applyInline(const nlohmann::json& source, MaterialAsset& target, const ResourceManager& resources);

    /**
     * @brief Build a material's canonical "inline" source descriptor (PBR scalars +
     * texture refs by name). This is both the material's editable source of
     * truth and its runtime form; the cooker writes it as the material's
     * library file.
     */
    nlohmann::json materialToInline(const MaterialAsset& material, const ResourceManager& resources);

} // namespace AssetSerializer

} // namespace Vkm::Engine
