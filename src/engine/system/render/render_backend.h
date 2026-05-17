#pragma once

#include <cstdint>

#include "resource/material_asset.h"
#include "resource/mesh_asset.h"

namespace Engine {
    class RenderTarget;
    struct RenderView;
    class ResourceManager;
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

        virtual void setWireframe(bool enabled) = 0;

        /**
         * @brief Synchronise backend-side GPU resources with the RenderView.
         *
         * Called by RenderSystem once per frame, after RenderView::build and
         * before pipeline.execute. Default no-op for backends that don't need
         * a separate sync step.
         */
        virtual void syncResources(const RenderView& view, const ResourceManager& resources) {}

        /**
         * @brief Resolve the multisampled scene color into a sampleable copy.
         *
         * Driven by the RenderGraph: invoked once before the first pass that
         * reads the resolved scene color, and again only after a later pass
         * writes the scene color. Default no-op for backends without MSAA.
         */
        virtual void resolveSceneColor() {}

        /**
         * @brief Enter offscreen "preview" mode at @p size x @p size.
         *
         * While active, every transient target accessor (HDR, G-buffer,
         * bloom, exposure, default target, ...) returns a private preview
         * set instead of the window-sized ones, so the unmodified render
         * graph can be re-executed into an offscreen texture. Paired with
         * endPreview(). Default: no-op (backend doesn't support previews).
         *
         * Editor-facing: the Material Editor / Asset Browser drive the real
         * pipeline through this so the preview can never drift from the
         * viewport.
         */
        virtual void beginPreview(uint32_t size) { (void)size; }

        /** @brief Leave preview mode; subsequent passes target the window. */
        virtual void endPreview() {}

        /**
         * @brief GL texture id of the last preview's composited color,
         *        usable as an ImGui ImTextureID (0 if unsupported / none).
         */
        virtual uint32_t previewColorTexture() const { return 0; }

        /**
         * @brief Copy the just-rendered preview into a stable per-key
         *        thumbnail texture and return its id.
         *
         * The single preview target is overwritten by the next render, so
         * grid thumbnails (Asset Browser) and the live Material Editor must
         * each own a persistent copy. @p key is caller-defined and opaque.
         * Default: unsupported.
         */
        virtual uint32_t snapshotPreviewToCache(uint64_t key, uint32_t size) {
            (void)key; (void)size; return 0;
        }

        /** @brief Cached thumbnail id for @p key, or 0 if never snapshotted. */
        virtual uint32_t cachedPreview(uint64_t key) const { (void)key; return 0; }

    protected:
        RenderBackendType m_type;
};

} // namespace Engine