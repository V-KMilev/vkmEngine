#pragma once

#include <memory>
#include <cstdint>

#include "render/render_pipeline.h"
#include "render/render_view.h"
#include "core/system.h"

namespace Engine {
    class RenderBackend;
    class RenderPass;
}

namespace Engine {

/**
 * @brief High-level orchestrator of rendering operations in the engine.
 *
 * The RenderSystem is responsible for:
 * - Owning and managing the current rendering backend (e.g., OpenGL, Optix, CPU).
 * - Managing the render pipeline (a sequence of render passes).
 * - Building a RenderView snapshot from the Scene every frame for consumption by the backend and passes.
 * - Handling backend and pipeline resizing in response to window or viewport changes.
 * - Serving as the main entry point to render a frame.
 *
 * RenderSystem is purely about rendering - it consumes resources but doesn't manage them.
 *
 * Usage flow:
 *  1. Set the backend with setBackend().
 *  2. Add render passes (such as forward, deferred, postprocess) via addPass().
 *  3. On window resize, call resize().
 *  4. For each frame, call renderFrame().
 */
class RenderSystem : public System {
    public:
        RenderSystem();
        ~RenderSystem() override;

        RenderSystem(const RenderSystem& other) = delete;
        RenderSystem& operator=(const RenderSystem& other) = delete;

        RenderSystem(RenderSystem && other) = delete;
        RenderSystem& operator=(RenderSystem && other) = delete;

    public:
        /**
         * @brief Switch the active rendering backend.
         *
         * Takes ownership of the new backend. If a viewport size is already set, resizes the backend accordingly.
         *
         * @param backend A unique pointer to the new RenderBackend to use.
         */
        void setBackend(std::unique_ptr<RenderBackend> backend);

        /**
         * @brief Get the current rendering backend.
         *
         * @return A reference to the current RenderBackend.
         */
        RenderBackend& getBackend() const { return *m_backend; }

        /**
         * @brief Add a new render pass to the pipeline.
         *
         * Render passes define the stages of the rendering process (e.g., G-buffer, lighting, postprocess).
         *
         * @param pass Unique pointer to a RenderPass to be added to the execution pipeline.
         */
        void addPass(std::unique_ptr<RenderPass> pass);

        /**
         * @brief Remove all render passes from the pipeline.
         *
         * The pipeline will be empty until passes are added again.
         */
        void clearPasses();

        /**
         * @brief Notify RenderSystem of a viewport size change.
         *
         * Resizes both the backend and the pipeline. This must be called whenever the framebuffer or window changes size.
         *
         * @param width New viewport width in pixels.
         * @param height New viewport height in pixels.
         */
        void resize(uint32_t width, uint32_t height);

        /**
         * @brief Render a frame using the current rendering backend and all passes in the pipeline.
         * @param ctx The shared FrameContext for this frame.
         */
        void update(FrameContext& ctx) override;

    private:
        std::unique_ptr<RenderBackend> m_backend;
        RenderPipeline m_pipeline;
        RenderView m_renderView;  ///< Persistent — vectors reuse capacity across frames.

        uint32_t m_width;
        uint32_t m_height;
};

} // namespace Engine
