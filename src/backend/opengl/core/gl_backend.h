#pragma once

#include <memory>
#include <cstdint>
#include <unordered_map>

#include "system/render/render_backend.h"

#include "gl_context.h"
#include "gl_view.h"
#include "gl_render_target.h"

namespace Engine {
    struct RenderView;
    class ResourceManager;
    class FrameResources;
}

namespace Engine {

/**
 * @brief OpenGL implementation of the RenderBackend interface.
 *
 * GLBackend bridges the high-level render system with the OpenGL API.
 * It manages the overall rendering workflow, including viewport resizing,
 * context handling, and mesh-resource management.
 *
 * This class prevents copy and move operations to guarantee a unique association
 * with OpenGL resources, which cannot be safely duplicated or transferred.
 */
class GLBackend : public RenderBackend {
    public:
        GLBackend();
        ~GLBackend() = default;

        GLBackend(const GLBackend& other) = delete;
        GLBackend& operator=(const GLBackend& other) = delete;

        GLBackend(GLBackend && other) = delete;
        GLBackend& operator=(GLBackend && other) = delete;

    public:
        /**
         * @brief Resize the render target or OpenGL viewport.
         *
         * Adjusts the OpenGL viewport and related render targets to match the provided dimensions.
         * Typically called in response to a window or framebuffer resize event.
         *
         * @param width  New width in pixels.
         * @param height New height in pixels.
         */
        void resize(uint32_t width, uint32_t height) override;
        RenderTarget& getDefaultTarget() override {
            // Backend always returns the window backbuffer. The graph routes
            // RGResource::Backbuffer at the offscreen preview target when a
            // preview session is open - that swap lives in RenderGraph, not
            // here, so the backend stays narrow.
            return m_defaultTarget;
        }
        void syncResources(const RenderView& view, const ResourceManager& resources) override;

        void ensurePreviewResourceTables(const RenderView& view,
                                          const ResourceManager& resources) override;

        std::unique_ptr<FrameResources>  createFrameResources() override;
        std::unique_ptr<RenderTarget>    createOffscreenTarget(uint32_t size) override;
        void registerPersistentResources(RenderGraph& graph) override;

        uint32_t snapshotToTexture(uint32_t srcTextureId, uint64_t key,
                                   uint32_t size) override;
        uint32_t cachedThumbnail(uint64_t key) const override;

        /// Drop a single cached thumbnail. Wire to asset-destruction events
        /// so a long editor session doesn't accumulate textures for assets
        /// the user has already removed. The cache key is the same uint64_t
        /// the asset browser passes through snapshotToTexture (materialKey /
        /// meshKey in asset_browser.cpp encode the asset handle id).
        void evictThumbnail(uint64_t key) override;

        /// Drop every cached thumbnail. Use on scene swap or wholesale
        /// asset-graph reset; cheaper than a per-key sweep when most
        /// entries are going away anyway.
        void clearThumbnailCache() override;

        // Editor preview rebinds the backbuffer after a preview session
        // since the composite pass left the offscreen FBO bound; without
        // this ImGui (which renders into the currently bound FBO) would
        // draw the whole editor into the preview texture.
        void rebindDefaultTarget() { m_defaultTarget.bind(); }

        /**
         * @brief Get the OpenGL rendering context.
         *
         * Provides access to the low-level OpenGL context for state management and rendering operations.
         * Use this for advanced OpenGL operations not exposed by the high-level API.
         *
         * @return Reference to the OpenGL context.
         */
        Core::Context& getContext() { return m_context; }

        /**
         * @brief Get the OpenGL rendering context (const version).
         *
         * Provides read-only access to the OpenGL context for querying state.
         *
         * @return Const reference to the OpenGL context.
         */
        const Core::Context& getContext() const { return m_context; }

        /**
         * @brief Get the OpenGL view for resource management.
         *
         * The GLView manages all GPU-side resources (meshes, materials, textures, lights)
         * and handles synchronization with the CPU-side resource manager.
         *
         * This is the main interface for:
         * - Syncing resources to GPU
         * - Accessing GPU resources during rendering
         * - Managing resource lifecycle and cleanup
         *
         * @return Reference to the internal GLView.
         */
        GLView& getView() { return m_view; }

        /**
         * @brief Get the OpenGL view (const version).
         *
         * Provides read-only access to the GLView for querying GPU resources.
         *
         * @return Const reference to the internal GLView.
         */
        const GLView& getView() const { return m_view; }

    private:
        Core::Context m_context;
        GLView m_view;
        GLDefaultRenderTarget m_defaultTarget;

        /// Per-key thumbnail snapshots (Asset Browser grid + the live
        /// Material Editor). The graph keeps a key->id index; the backend
        /// owns the GL textures themselves.
        std::unordered_map<uint64_t, std::unique_ptr<Core::Texture2D>> m_thumbCache;
};

} // namespace Engine
