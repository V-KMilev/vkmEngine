#pragma once

#include <nlohmann/json.hpp>

#include "resource/asset/material_asset.h"

namespace Engine {

class ResourceManager;
class Scene;

/**
 * @brief Serialize / deserialize the asset graph referenced by a Scene.
 *
 * Saves emit the meshes, materials, and textures actually referenced by the
 * scene. Loads recreate them by dispatching each descriptor through the
 * AssetFactory seam (io/asset_factory.h); assets with no source are skipped on
 * save (silently - they can't be recreated anyway), and assets already in
 * ResourceManager (by name) are skipped on load (idempotent re-loads).
 *
 * Textures are their own top-level section: saveAssetsForScene emits a
 * `textures` array of every map a material references, and loadAssets
 * recreates them via the texture factory before materials resolve their refs.
 */
namespace AssetSerializer {

    nlohmann::json saveAssetsForScene(const Scene& scene, const ResourceManager& resources);
    bool loadAssets(const nlohmann::json& assetsJson, ResourceManager& resources);

    /**
     * @brief Apply an "inline" material descriptor (kind=="inline") to a freshly-
     * constructed MaterialAsset. Used by the inline-material factory in
     * tools/asset_registration.cpp.
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
