#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "resource/material_asset.h"
#include "resource/mesh_asset.h"
#include "resource/texture_asset.h"

#include "gl_instance_batcher.h"
#include "resource/gl_camera.h"
#include "resource/gl_lights.h"
#include "resource/gl_material.h"
#include "resource/gl_mesh.h"
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
 * purged based on visibility — a camera that pans away from everything
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

        /// Lookup: returns nullptr if not synced or out of range.
        const GLMesh*     getMesh(const MeshHandle& handle) const;
        const GLMaterial* getMaterial(const MaterialHandle& handle) const;
        const GLTexture*  getTexture(const TextureHandle& handle) const;
        GLMesh*           getMutableMesh(const MeshHandle& handle);

        const GLLights& getLights() const { return m_lights; }

        /// Builds instance batches from the (already sorted) drawables.
        void buildInstanceBatches(const RenderView& renderView);

        GLInstanceBatcher&       getInstanceBatcher()       { return m_instanceBatcher; }
        const GLInstanceBatcher& getInstanceBatcher() const { return m_instanceBatcher; }

    private:
        /// Reconcile a single resource table against a deduped handle list.
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

        GLCamera          m_camera;
        GLLights          m_lights;
        GLInstanceBatcher m_instanceBatcher;

        uint64_t m_lastMeshTypeVersion     = 0;
        uint64_t m_lastMaterialTypeVersion = 0;
        uint64_t m_lastTextureTypeVersion  = 0;
        size_t   m_lastDrawableCount       = 0;
};

} // namespace Engine
