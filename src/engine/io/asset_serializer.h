#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "resource/material_asset.h"
#include "resource/mesh_asset.h"
#include "resource/texture_asset.h"

namespace Engine {

class ResourceManager;
class Scene;

/**
 * @brief Registry of factories that recreate assets from their source JSON.
 *
 * Resolving the layering: engine/io can't directly call into tools/ (mesh
 * generators, material loaders) without inverting the dependency. So tools
 * registers factory lambdas here at startup; AssetSerializer dispatches
 * through this registry instead of hard-coding generator names.
 *
 * The map key is the source `kind` field — e.g. "generator" for procedural
 * meshes, "folder" for folder-loaded materials. A future "file" kind for
 * glTF mesh imports plugs in the same way.
 */
class AssetFactories {
    public:
        using MeshFactory     = std::function<MeshAsset     (const nlohmann::json& desc)>;
        using TextureFactory  = std::function<TextureHandle (const nlohmann::json& desc, ResourceManager& resources)>;
        using MaterialFactory = std::function<MaterialHandle(const nlohmann::json& desc, ResourceManager& resources)>;

        static AssetFactories& get();

        void registerMesh    (std::string kind, MeshFactory     factory);
        void registerTexture (std::string kind, TextureFactory  factory);
        void registerMaterial(std::string kind, MaterialFactory factory);

        /// Look up and invoke. Returns an empty MeshAsset if `kind` is unknown.
        MeshAsset      createMesh    (const nlohmann::json& source) const;
        /// Returns an invalid handle if `kind` is unknown.
        TextureHandle  createTexture (const nlohmann::json& source, ResourceManager& resources) const;
        MaterialHandle createMaterial(const nlohmann::json& source, ResourceManager& resources) const;

    private:
        std::unordered_map<std::string, MeshFactory>     m_meshFactories;
        std::unordered_map<std::string, TextureFactory>  m_textureFactories;
        std::unordered_map<std::string, MaterialFactory> m_materialFactories;
};

/**
 * @brief Serialize / deserialize the asset graph referenced by a Scene.
 *
 * Saves emit only the meshes + materials actually referenced by Mesh
 * components in the scene. Loads recreate them by dispatching each
 * descriptor through AssetFactories; assets with no source are skipped on
 * save (silently — they can't be recreated anyway), and same-named assets
 * already in ResourceManager are skipped on load (idempotent re-loads).
 *
 * Textures aren't a top-level concept here: the material `folder` loader
 * rediscovers and loads them from disk.
 */
namespace AssetSerializer {

    nlohmann::json saveAssetsForScene(const Scene& scene, const ResourceManager& resources);
    bool loadAssets(const nlohmann::json& assetsJson, ResourceManager& resources);

    /// Apply an "inline" material descriptor (kind=="inline") to a freshly-
    /// constructed MaterialAsset. Used by the inline-material factory in
    /// tools/asset_registration.cpp.
    void applyInline(const nlohmann::json& source, MaterialAsset& target, const ResourceManager& resources);

} // namespace AssetSerializer

} // namespace Engine
