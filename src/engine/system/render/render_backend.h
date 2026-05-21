#pragma once

#include <cstdint>
#include <memory>

#include "resource/material_asset.h"
#include "resource/mesh_asset.h"

namespace Engine {
    class RenderTarget;
    struct RenderView;
    class ResourceManager;
    class RenderGraph;
    class FrameResources;
}

namespace Engine {

/**
 * @brief Enumeration of supported rendering backend types.
 */
enum class RenderBackendType {
    NONE    = 0,    ///< No backend specified or uninitialized.
    OpenGL  = 1,    ///< OpenGL-based rendering backend.
    Optix   = 2,    ///< NVIDIA Optix ray tracing rendering backend.
    CPU     = 3,    ///< CPU/software-based raytracing backend.
};

/**
 * @brief Convert a ComponentType enum value to its string representation.
 *
 * @param type The RenderBackendType value to convert.
 * @return const char* String representation of the RenderBackendType.
 */
 constexpr const char* toString(RenderBackendType type) {
    switch (type) {
        case RenderBackendType::NONE:    return "NONE";
        case RenderBackendType::OpenGL:  return "OpenGL";
        case RenderBackendType::Optix:   return "Optix";
        case RenderBackendType::CPU:     return "CPU";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Abstract base class for all rendering backends.
 *
 * Provides the interface all concrete rendering backends (OpenGL, Optix, CPU, etc.)
 * must implement in order to integrate with the rendering system. Prevents
 * copying/moving of instances.
 */
class RenderBackend {
    public:
        /**
         * @brief Construct a RenderBackend with a specific backend type.
         * @param type The RenderBackendType associated with the backend.
         */
        RenderBackend(RenderBackendType type) : m_type(type) {}
        virtual ~RenderBackend() = default;

        RenderBackend(const RenderBackend& other) = delete;
        RenderBackend& operator=(const RenderBackend& other) = delete;

        RenderBackend(RenderBackend && other) = delete;
        RenderBackend& operator=(RenderBackend && other) = delete;

    public:
        /**
         * @brief Get the type of the backend.
         * @return The RenderBackendType of the backend.
         */
        RenderBackendType getType() const { return m_type; }

        /**
         * @brief Resize the backend's render targets or framebuffers.
         * @param width New width in pixels.
         * @param height New height in pixels.
         */
        virtual void resize(uint32_t width, uint32_t height) = 0;

        /**
         * @brief Get the default render target (typically the screen framebuffer).
         * @return Reference to the default RenderTarget.
         */
        virtual RenderTarget& getDefaultTarget() = 0;

        /**
         * @brief Synchronise backend-side GPU resources with the RenderView.
         *
         * Called by RenderSystem once per frame, after RenderView::build and
         * before pipeline.execute. Default no-op for backends that don't need
         * a separate sync step.
         */
        virtual void syncResources(const RenderView& view, const ResourceManager& resources) {}

        /**
         * @brief Force every mesh/material/texture in @p view resident on
         *        the GPU before the next syncResources.
         *
         * Editor previews construct a tiny hand-built RenderView (one
         * preview shape + one preview material) that can slip past sync's
         * version-gating heuristics. RenderSystem calls this on the
         * preview path before syncResources to force those handles to
         * upload. Default no-op for backends that don't need it.
         */
        virtual void ensurePreviewResourceTables(const RenderView& view,
                                                  const ResourceManager& resources) {
            (void)view; (void)resources;
        }

        /**
         * @brief Publish persistent resources into the graph's pool.
         *
         * For resources whose lifetime exceeds a frame and which don't get
         * swapped by editor previews (e.g. the shadow atlas, the IBL set).
         * Called once by the graph the first time it executes. The default
         * is a no-op; backends override to register what they expose.
         */
        virtual void registerPersistentResources(RenderGraph& /*graph*/) {}

        /**
         * @brief Construct a backend-specific FrameResources pool.
         *
         * The graph holds the returned pool via unique_ptr<FrameResources>
         * and reaches it through the engine-side abstract interface (resize
         * + registerWith + resolveSceneColor). Backends decide what
         * concrete sub-resources go in the pool; the graph just publishes
         * them into its typed lookup via FrameResources::registerWith.
         *
         * Called by the graph at first resize/execute and again when the
         * editor opens an offscreen preview (which needs its own pool at
         * the preview size).
         */
        virtual std::unique_ptr<FrameResources> createFrameResources() = 0;

        /**
         * @brief Construct a backend-specific offscreen RenderTarget at
         *        (size, size) for an editor material preview.
         *
         * Returned via unique_ptr<RenderTarget>; the graph owns it for the
         * lifetime of the preview session and routes RGResource::Backbuffer
         * to it while the preview is active. Default: nullptr (backend
         * doesn't support previews).
         */
        virtual std::unique_ptr<RenderTarget> createOffscreenTarget(uint32_t size) {
            (void)size; return nullptr;
        }

        /**
         * @brief Backend-specific stable thumbnail copy.
         *
         * The graph's offscreen preview target gets overwritten by the next
         * preview, so editor surfaces that need a persistent texture
         * (Asset Browser grid, Material Editor live frame) ask the graph
         * to snapshot per-key. The graph delegates the GL-level copy here.
         *
         * Implementations should be idempotent on @p key (reuse the
         * existing storage when present); the graph maintains the cache.
         *
         * @param srcTextureId  Backend-typed texture id to copy from
         *                      (the preview target's color attachment).
         * @param size          Square thumbnail size in pixels.
         * @return Backend-typed texture id of the cached snapshot. 0 on
         *         failure or when the backend doesn't support thumbnails.
         */
        virtual uint32_t snapshotToTexture(uint32_t srcTextureId, uint64_t key,
                                            uint32_t size) {
            (void)srcTextureId; (void)key; (void)size; return 0;
        }

        /** @brief Cached thumbnail id for @p key, or 0 if never snapshotted. */
        virtual uint32_t cachedThumbnail(uint64_t key) const { (void)key; return 0; }

        /**
         * @brief Drop a single cached thumbnail.
         *
         * Backends that don't keep a cache (or haven't snapshotted @p key
         * yet) treat this as a no-op.
         */
        virtual void evictThumbnail(uint64_t key) { (void)key; }

        /**
         * @brief Drop every cached thumbnail.
         *
         * Same no-op semantics on backends without a cache.
         */
        virtual void clearThumbnailCache() {}

    protected:
        RenderBackendType m_type;
};

} // namespace Engine