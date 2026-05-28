#pragma once

#include <memory>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "system/render/render_backend.h"

#include "gl_context.h"
#include "gl_view.h"
#include "gl_render_target.h"

namespace Engine {

struct RenderView;
class ResourceManager;
class FrameResources;

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
        ~GLBackend() override;

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
            // Backend always returns the window backbuffer. The graph
            // routes RGResource::Backbuffer at a caller-pushed offscreen
            // target when one is active - that swap lives in RenderGraph,
            // not here, so the backend stays narrow.
            return m_defaultTarget;
        }
        void syncResources(const RenderView& view, const ResourceManager& resources) override;

        void ensureResourcesResident(
            const RenderView& view,
            const ResourceManager& resources
        ) override;

        std::unique_ptr<FrameResources>  createFrameResources() override;
        std::unique_ptr<RenderTarget>    createOffscreenTarget(uint32_t size) override;
        void registerPersistentResources(RenderGraph& graph) override;

        /// Drain completed Tracy GPU timer queries. Called by RenderGraph
        /// at the end of every frame; no-op when VKM_PROFILER is off.
        void endFrame() override;

        /**
         * @brief Per-pass GPU timer scope.
         *
         * Double-buffered with GL_TIME_ELAPSED queries; reading the "other"
         * slot before issuing this frame's query gives the driver a full
         * frame to make results available, avoiding the stall a same-frame
         * readback would cause.
         */
        void beginPassTimer(std::size_t passIndex) override;
        void endPassTimer  (std::size_t passIndex) override;

        std::vector<ShaderVariantStat> shaderVariantStats() const override;

        /// CPU mirror of the auto-exposure adapted luminance; written each
        /// frame by GLExposurePass via setAdaptedLuminance(), read by the
        /// editor's Exposure card for the adapted-EV readout.
        float getAdaptedLuminance() const override { return m_adaptedLuminance; }
        void  setAdaptedLuminance(float v)         { m_adaptedLuminance = v; }

        /// Read a rectangle of the GL_BACK buffer as RGB8, top-down.
        /// Saves and restores GL_PACK_ALIGNMENT and GL_READ_BUFFER.
        bool readbackPixels(
            uint32_t x,
            uint32_t y,
            uint32_t w,
            uint32_t h,
            uint32_t windowHeight,
            std::vector<uint8_t>& outRGB
        ) override;

        const char* apiName()    const override { return "OpenGL"; }
        std::string apiVersion() const override;
        std::string deviceName() const override;

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

        /// Last CPU readback of the auto-exposure adapted luminance. Seeded
        /// at the same value GLAutoExposure seeds its 1x1 ping-pong with
        /// (0.18) so the editor's readout shows something sensible before
        /// the exposure pass runs the first time.
        float m_adaptedLuminance = 0.18f;

        /**
         * @brief Per-pass GL_TIME_ELAPSED queries (two slots per pass).
         *
         * A frame issues the new query while the older slot's result
         * completes asynchronously. queries[] == 0 means "not yet
         * allocated"; the slot is lazy-generated on first beginPassTimer
         * and freed in the dtor.
         */
        struct PassQueryRing {
            unsigned int queries[2] = {0u, 0u};
            bool         issued[2]  = {false, false};
            int          currentSlot = -1;   ///< Slot of the in-flight (this-frame) query.
        };
        std::vector<PassQueryRing> m_passQueries;

        void destroyPassTimers();
};

} // namespace Engine
