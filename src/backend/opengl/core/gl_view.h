#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "resource/material_asset.h"
#include "resource/mesh_asset.h"
#include "resource/shader_asset.h"
#include "resource/texture_asset.h"

#include "gl_instance_batcher.h"
#include "resource/gl_camera.h"
#include "resource/gl_lights.h"
#include "resource/gl_material.h"
#include "resource/gl_mesh.h"
#include "resource/gl_shader_program.h"
#include "resource/gl_shadow_data.h"
#include "resource/gl_shadow_map.h"
#include "resource/gl_ibl.h"
#include "resource/gl_texture.h"

namespace Engine {

struct RenderView;
class ResourceManager;

/**
 * @brief Dense table of GPU resources keyed by handle.id().
 *
 * Parallel vectors: the GL wrapper (unique_ptr, null = absent) and the
 * version last seen from the CPU-side asset. Sized to max(handle.id()) + 1.
 * Handle IDs are dense by construction (the ResourceManager's per-type
 * SlotAllocator reuses freed slots), so the vector stays compact.
 *
 * Entries live as long as the asset lives in ResourceManager. They are NOT
 * purged based on visibility - a camera that pans away from everything
 * shouldn't trigger GPU resource churn.
 */
template<typename GLT>
struct GLResourceTable {
    std::vector<std::unique_ptr<GLT>> entries;
    std::vector<uint64_t>             versions;
};

/**
 * @brief GLView manages OpenGL-side resources mirroring ResourceManager.
 *
 * Single sync() entry point uploads/updates GPU representations of meshes,
 * materials, textures, and lights referenced by the current RenderView.
 */
class GLView {
    public:
        GLView() = default;
        ~GLView();

        GLView(const GLView& other) = delete;
        GLView& operator=(const GLView& other) = delete;

        GLView(GLView && other) = delete;
        GLView& operator=(GLView && other) = delete;

    public:
        /**
         * @brief Synchronise all GPU resources referenced by the RenderView.
         *
         * Meshes, materials, textures, and lights are all reconciled in one pass.
         * Skips work when nothing relevant has changed since last frame.
         */
        void sync(const RenderView& view, const ResourceManager& resources);

        /**
         * @brief Sync (compile or recompile-if-version-bumped) and return the
         *
         * backend shader for a handle, in one call. Used by render passes
         * at draw time; this is how hot reload reaches the GPU side -
         * the watcher commits -> asset.version bumps -> the next resolve
         * call rebuilds the program.
         *
         * Returns nullptr only when the handle is empty or invalid.
         */
        GLShader* resolveShader(const ShaderHandle& handle, const ResourceManager& resources);

        /**
         * @brief Resolve a per-material variant of @p handle, compiled with the
         *
         * #defines that match @p featureFlags (a MaterialFeature bitset).
         *
         * First call for a given (handle, flags) pair compiles a fresh
         * GLShader with the right defines and caches it; subsequent calls
         * return the cached program. When the underlying asset version
         * bumps (hot reload of the .shader file), every variant for that
         * asset is dropped and rebuilt lazily on the next resolve.
         *
         * flags == 0 is a legitimate variant ("no optional features") and
         * gets its own cache entry; it does NOT collapse to the ubershader
         * path. The whole point of the variant cache is that flags == 0
         * compiles to a much smaller program than the ubershader.
         *
         * Returns nullptr only when the handle is empty or invalid.
         */
        GLShader* resolveShaderVariant(const ShaderHandle& handle,
                                       uint32_t featureFlags,
                                       const ResourceManager& resources);

        /// Ensure a GPU material/mesh exists and is up to date for @p handle
        /// even when no scene entity references it (editor previews, asset
        /// browser). Same lazy build-or-rebuild path as resolveShader.
        const GLMaterial* ensureMaterial(const MaterialHandle& handle, const ResourceManager& resources);
        GLMesh*           ensureMesh(const MeshHandle& handle, const ResourceManager& resources);

        /**
         * @brief Ensure every texture a material references is GPU-resident. sync()
         *
         * only uploads textures on its coarse dirty check; per-asset previews
         * (Asset Browser grid) render many one-drawable views per frame and
         * would otherwise reuse a stale/empty texture table.
         */
        void ensureMaterialTextures(const MaterialHandle& handle, const ResourceManager& resources);

        /// Lookup: returns nullptr if not synced or out of range.
        const GLMesh*     getMesh(const MeshHandle& handle) const;
        const GLMaterial* getMaterial(const MaterialHandle& handle) const;
        const GLTexture*  getTexture(const TextureHandle& handle) const;
        GLMesh*           getMutableMesh(const MeshHandle& handle);

        const GLLights& getLights() const { return m_lights; }

        GLShadowAtlas&        getShadowAtlas()        { return m_shadowAtlas; }
        const GLShadowAtlas&  getShadowAtlas()  const { return m_shadowAtlas; }
        GLShadowData&         getShadowData()         { return m_shadowData; }
        const GLShadowData&   getShadowData()   const { return m_shadowData; }

        GLIBL&                getIBL()                { return m_ibl; }
        const GLIBL&          getIBL()          const { return m_ibl; }

        GLInstanceBatcher&       getInstanceBatcher()       { return m_instanceBatcher; }
        const GLInstanceBatcher& getInstanceBatcher() const { return m_instanceBatcher; }

        /// Separate batch list for the shadow pass, built from the full-scene
        /// shadow-caster set (NOT the camera-culled drawables).
        GLInstanceBatcher&       getShadowBatcher()       { return m_shadowBatcher; }
        const GLInstanceBatcher& getShadowBatcher() const { return m_shadowBatcher; }

    private:
        /**
         * @brief Reconcile a single GL-side resource table against a
         *        deduplicated handle list from this frame's RenderView.
         *
         * For each unique handle, ensures the table has a slot at handle.id()
         * and rebuilds the GL wrapper when the asset's version has changed
         * since last seen. Entries the scene no longer references are left
         * in place (a camera that pans away from everything shouldn't
         * trigger GPU churn).
         *
         * @tparam AssetT CPU asset type (MeshAsset, TextureAsset, ...).
         * @tparam GLT    GL wrapper type (GLMesh, GLTexture, ...).
         */
        template<typename AssetT, typename GLT>
        void syncTable(
            GLResourceTable<GLT>& table,
            const std::vector<Handle<AssetT>>& handles,
            const ResourceManager& resources
        );

    private:
        GLResourceTable<GLMesh>     m_meshTable;
        GLResourceTable<GLMaterial> m_materialTable;
        GLResourceTable<GLTexture>  m_textureTable;
        GLResourceTable<GLShader>   m_shaderTable;

        /**
         * @brief Per-material shader-variant cache. Outer key is shaderId; inner
         *
         * key packs (generation, flags) into a uint64 so slot recycles by
         * SlotAllocator and feature-flag sets share the same bucket but
         * cannot collide. Eviction of all variants for one shader (on hot
         * reload or slot recycle) is the inner map's clear(), no global
         * scan needed and no parallel index to keep in sync.
         */
        struct VariantEntry {
            std::unique_ptr<GLShader> program;
            uint64_t                  assetVersion = 0;
        };
        static constexpr uint64_t variantSubkey(uint32_t generation, uint32_t flags) {
            return (static_cast<uint64_t>(generation) << 32) | flags;
        }
        std::unordered_map<uint32_t /*shaderId*/,
            std::unordered_map<uint64_t /*subkey*/, VariantEntry>>
            m_shaderVariants;

        GLCamera          m_camera;
        GLLights          m_lights;
        GLShadowAtlas     m_shadowAtlas{
            GLConfig::Limits::ShadowResolution2D,
            GLConfig::Limits::ShadowResolutionCube,
            Config::MaxShadowCasters2D,
            Config::MaxShadowCastersCube
        };
        GLShadowData      m_shadowData;
        GLIBL             m_ibl;
        GLInstanceBatcher m_instanceBatcher;
        GLInstanceBatcher m_shadowBatcher;

        uint64_t m_lastMeshTypeVersion     = 0;
        uint64_t m_lastMaterialTypeVersion = 0;
        uint64_t m_lastTextureTypeVersion  = 0;
        size_t   m_lastDrawableCount       = 0;
};

} // namespace Engine
