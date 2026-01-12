#pragma once

#include <memory>
#include <cstdint>

#include "render_pipeline.h"

namespace Engine {
    class ResourceManager;
    struct RenderView;
    class Scene;

    class RenderBackend;
    class RenderPass;
}

namespace Engine {

/**
 * @brief High-level orchestrator of rendering operations in the engine.
 *
 * The RenderManager is responsible for:
 * - Owning and managing the current rendering backend (e.g., OpenGL, Optix, CPU).
 * - Managing the render pipeline (a sequence of render passes).
 * - Building a RenderView snapshot from the Scene every frame for consumption by the backend and passes.
 * - Handling backend and pipeline resizing in response to window or viewport changes.
 * - Serving as the main entry point to render a frame.
 *
 * RenderManager is purely about rendering - it consumes resources but doesn't manage them.
 *
 * Usage flow:
 *  1. Set the backend with setBackend().
 *  2. Add render passes (such as forward, deferred, postprocess) via addPass().
 *  3. On window resize, call resize().
 *  4. For each frame, call renderFrame().
 */
class RenderManager {
    public:
        RenderManager();
        ~RenderManager();

        RenderManager(const RenderManager& other) = delete;
        RenderManager& operator=(const RenderManager& other) = delete;

        RenderManager(RenderManager && other) = delete;
        RenderManager& operator=(RenderManager && other) = delete;

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
         * @brief Notify RenderManager of a viewport size change.
         *
         * Resizes both the backend and the pipeline. This must be called whenever the framebuffer or window changes size.
         *
         * @param width New viewport width in pixels.
         * @param height New viewport height in pixels.
         */
        void resize(uint32_t width, uint32_t height);

        /**
         * @brief Render a frame using the current backend and pipeline.
         *
         * Builds a RenderView from the scene, then executes the pipeline.
         *
         * @param scene The current scene (entities, cameras, etc.).
         * @param resources Reference to the ResourceManager for this frame.
         * @param viewportWidth The width of the rendering viewport in pixels.
         * @param viewportHeight The height of the rendering viewport in pixels.
         */
        void renderFrame(
            const Scene& scene,
            const ResourceManager& resources,
            uint32_t viewportWidth,
            uint32_t viewportHeight
        );

    private:
        std::unique_ptr<RenderBackend> m_backend;
        RenderPipeline m_pipeline;

        uint32_t m_width;
        uint32_t m_height;
};

} // namespace Engine
