#pragma once

#include <vector>

#include <nlohmann/json.hpp>

#include "ecs/entity.h"
#include "resource/asset/material_asset.h"

namespace Engine {

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

    nlohmann::json saveAssetsForScene(const Scene& scene, const ResourceManager& resources);
    bool loadAssets(const nlohmann::json& assetsJson, ResourceManager& resources);

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

} // namespace Engine
