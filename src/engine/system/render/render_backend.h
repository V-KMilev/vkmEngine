#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "resource/material_asset.h"
#include "resource/mesh_asset.h"
#include "system/render/render_target.h"  // unique_ptr<RenderTarget> needs complete type

namespace Engine {

struct RenderView;
class ResourceManager;
class RenderGraph;
class FrameResources;

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
         * syncResources gates GPU-table uploads on version + drawable-count
         * deltas. A hand-built RenderView (editor previews, future capture
         * paths) can slip past that heuristic, so callers in that situation
         * call this first to force the referenced handles to upload.
         * Default no-op for backends that don't need it.
         */
        virtual void ensureResourcesResident(
            const RenderView& view,
            const ResourceManager& resources
        ) {
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
         * @brief Per-frame backend tail. Called by RenderGraph::execute after
         *        every pass has run.
         *
         * The OpenGL backend uses this to drain completed Tracy GPU timer
         * queries from this and prior frames; other backends typically have
         * nothing to do here. Stays an abstract hook so RenderGraph never
         * needs to include backend headers.
         */
        virtual void endFrame() {}

        /**
         * @brief Per-pass GPU timer scope - backend-provided primitive used
         *        by the render graph to feed @ref GpuTimingPool.
         *
         * Wraps a single pass's GPU work. Implementations are expected to
         * issue a GPU-side begin/end timer (e.g. GL_TIME_ELAPSED) and to
         * drain completed measurements into the timing pool from their
         * own @ref endFrame() hook, with whatever frame-lag the API
         * requires to avoid CPU stalls on the read-back.
         *
         * Default: no-op (a backend without GPU timer support quietly
         * shows zeros in the editor).
         */
        virtual void beginPassTimer(std::size_t /*passIndex*/) {}
        virtual void endPassTimer(std::size_t /*passIndex*/) {}

        /**
         * @brief Snapshot of the backend's shader variant cache.
         *
         * Surfaced in the editor's GPU panel so users can spot variant
         * explosion. Empty default keeps backends without a variant cache
         * (or backends where the cache isn't worth surfacing) quiet.
         */
        struct ShaderVariantStat {
            std::uint32_t shaderId = 0;
            std::string   name;
            std::size_t   variants = 0;
        };
        virtual std::vector<ShaderVariantStat> shaderVariantStats() const { return {}; }

        /**
         * @brief Latest CPU-side mirror of the auto-exposure adapted luminance.
         *
         * The exposure pass reads back the 1x1 R16F target after each frame
         * and parks it here so the editor's Exposure card can display the
         * adapted EV without doing its own GPU sync. Returns the seed value
         * (0.18) on backends that haven't implemented auto-exposure.
         */
        virtual float getAdaptedLuminance() const { return 0.18f; }

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
         *        (size, size).
         *
         * The caller owns the returned target and routes whatever drawing
         * path it controls (push it onto the render graph, render once for
         * a screenshot, etc.) at it. Default: nullptr (backend doesn't
         * support offscreen targets).
         */
        virtual std::unique_ptr<RenderTarget> createOffscreenTarget(uint32_t size) {
            (void)size; return nullptr;
        }

        /**
         * @brief Read a sub-rect of the swapchain into RGB8 pixels.
         *
         * Used by the editor for screenshots. Rect is in window-pixel
         * coords with ImGui's y-down convention; the backend takes care
         * of any internal flips so @p outRGB is top-down (PNG-friendly).
         * @p windowHeight is the full window height (needed by backends
         * whose framebuffer origin differs from ImGui).
         *
         * Default returns false (backend doesn't expose readback).
         *
         * @param x,y,w,h        Rect in window pixels, ImGui y-down.
         * @param windowHeight   Full window height in pixels.
         * @param outRGB         Resized to w*h*3 bytes on success.
         * @return true on success, false on invalid rect / unsupported.
         */
        virtual bool readbackPixels(
            uint32_t x,
            uint32_t y,
            uint32_t w,
            uint32_t h,
            uint32_t windowHeight,
            std::vector<uint8_t>& outRGB
        ) {
            (void)x; (void)y; (void)w; (void)h;
            (void)windowHeight; (void)outRGB;
            return false;
        }

        /// Short name of the graphics API ("OpenGL", "Vulkan", "CPU").
        virtual const char* apiName() const { return toString(m_type); }

        /// Runtime API version string, or "" if unavailable.
        virtual std::string apiVersion() const { return {}; }

        /// Human-readable device / renderer name, or "" if unavailable.
        virtual std::string deviceName() const { return {}; }

    protected:
        RenderBackendType m_type;
};

} // namespace Engine