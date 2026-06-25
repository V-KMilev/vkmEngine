#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/shader_asset.h"
#include "resource/asset/texture_asset.h"

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
 * The map key is the source `kind` field - e.g. "generator" for procedural
 * meshes, "folder" for folder-loaded materials. A future "file" kind for
 * glTF mesh imports plugs in the same way.
 */
class AssetFactories {
    public:
        using MeshFactory     = std::function<MeshHandle    (const nlohmann::json& desc, ResourceManager& resources)>;
        using TextureFactory  = std::function<TextureHandle (const nlohmann::json& desc, ResourceManager& resources)>;
        using MaterialFactory = std::function<MaterialHandle(const nlohmann::json& desc, ResourceManager& resources)>;
        using ShaderFactory   = std::function<ShaderAsset   (const nlohmann::json& desc)>;

        static AssetFactories& get();

        void registerMesh    (std::string kind, MeshFactory     factory);
        void registerTexture (std::string kind, TextureFactory  factory);
        void registerMaterial(std::string kind, MaterialFactory factory);
        void registerShader  (std::string kind, ShaderFactory   factory);

        /// Returns an invalid handle if `kind` is unknown.
        MeshHandle     createMesh    (const nlohmann::json& source, ResourceManager& resources) const;
        TextureHandle  createTexture (const nlohmann::json& source, ResourceManager& resources) const;
        MaterialHandle createMaterial(const nlohmann::json& source, ResourceManager& resources) const;
        ShaderAsset    createShader  (const nlohmann::json& source) const;

    private:
        std::unordered_map<std::string, MeshFactory>     m_meshFactories;
        std::unordered_map<std::string, TextureFactory>  m_textureFactories;
        std::unordered_map<std::string, MaterialFactory> m_materialFactories;
        std::unordered_map<std::string, ShaderFactory>   m_shaderFactories;
};

/**
 * @brief Serialize / deserialize the asset graph referenced by a Scene.
 *
 * Saves emit the meshes, materials, and textures actually referenced by the
 * scene. Loads recreate them by dispatching each
 * descriptor through AssetFactories; assets with no source are skipped on
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

    /// Apply an "inline" material descriptor (kind=="inline") to a freshly-
    /// constructed MaterialAsset. Used by the inline-material factory in
    /// tools/asset_registration.cpp.
    void applyInline(const nlohmann::json& source, MaterialAsset& target, const ResourceManager& resources);

    /// Build a material's canonical "inline" source descriptor (PBR scalars +
    /// texture refs by name). This is both the material's editable source of
    /// truth and its runtime form; the cooker writes it as the material's
    /// library file.
    nlohmann::json materialToInline(const MaterialAsset& material, const ResourceManager& resources);

} // namespace AssetSerializer

} // namespace Engine
