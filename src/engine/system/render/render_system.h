#pragma once

#include <functional>
#include <memory>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <vector>

#include "system/render/render_graph.h"
#include "system/render/render_view.h"
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

        /// Only reads ResourceManager (and only inside the render thread's
        /// graph execute, when run with overlap). update() itself reads but
        /// never writes Resources.
        bool mutatesResources() const override { return false; }

        /**
         * @brief Split update() so the view-build happens on the main
         *        thread while the previous frame's render is still in
         *        flight on the render thread.
         *
         * buildView() fills m_views[frameIndex & 1] from the live Scene,
         * ResourceManager, and Visibility. Must be called on the main
         * thread between waitForFrame() of frame K-1 and postFrame() of
         * frame K. executeFrame() reads the same buffer index, runs the
         * render graph + backend sync; it is the body of the lambda
         * posted to RenderThread.
         *
         * When the render thread is disabled, update() calls both in
         * sequence on the main thread, indexed 0.
         */
        void buildView(FrameContext& ctx, uint32_t frameIndex);
        void executeFrame(FrameContext& ctx, uint32_t frameIndex);

        /**
         * @brief Narrow pass introspection for editor / debug tooling.
         *
         * Lets the Environment Inspector's advanced "Pipeline" tab list
         * passes and force-toggle them without #include "render_graph.h"
         * or knowledge of the pass class hierarchy. The graph itself stays
         * an engine internal; this surface is what's stable for tools.
         */
        size_t           passCount() const;
        std::string_view passName(size_t index) const;
        bool             isPassEnabled(size_t index) const;
        void             setPassEnabled(size_t index, bool enabled);

        /// Read-only graph accessor for editor tools (render-graph
        /// visualizer). Stays const so panels can't mutate the schedule.
        const RenderGraph& getGraph() const { return m_graph; }

        EnvironmentConfig& getEnvironment() { return m_environment; }
        const EnvironmentConfig& getEnvironment() const { return m_environment; }

        /**
         * @brief Invalidate temporal post-processing history (TAA).
         *
         * Call after any view discontinuity - scene load, camera teleport,
         * "frame on selection" jumps, etc. Without this, TAA reprojects
         * across the discontinuity and smears the new view for several
         * frames. Cheap; just flips the primed flag on the history buffer.
         */
        void invalidateTemporalHistory();

        /**
         * @brief Queue a callable to run on the backend's thread inside
         *        the next executeFrame(), before the scene render.
         *
         * Generic mechanism for any code path that needs backend work but
         * doesn't itself hold the backend's context. In single-threaded
         * mode the caller IS the backend thread, so an immediate inline
         * call would be equivalent and faster - prefer that. This API
         * exists for the render-thread case: editor panels rendering
         * previews, future screenshot capture, IBL re-bake from a menu.
         *
         * Thread-safe; jobs run in queue order. Lifetime: jobs are
         * one-shot and dropped after execution. Callers capture any
         * dependencies they need; references must outlive the job
         * (typically Engine-owned state which lives forever).
         */
        void queueBackendJob(std::function<void()> job);

    private:
        std::unique_ptr<RenderBackend> m_backend;
        RenderGraph m_graph;

        /// Double-buffered RenderView for the render-thread overlap. Main
        /// writes m_views[frameIndex & 1] in buildView(); the render
        /// thread reads the same buffer in executeFrame(). The next frame
        /// uses the OTHER buffer, so main's buildView never touches the
        /// buffer the render thread is currently reading. Vectors keep
        /// their capacity across frames.
        RenderView m_views[2];

        /// Thread-safe queue of one-shot backend jobs. Producers (panels,
        /// editor commands) call queueBackendJob() from any thread; the
        /// render thread drains and runs them at the top of executeFrame(),
        /// before the scene render and ImGui draw step.
        std::vector<std::function<void()>> m_pendingBackendJobs;
        std::mutex                         m_pendingBackendJobsMutex;

        EnvironmentConfig m_environment;

        uint32_t m_width  = 0;
        uint32_t m_height = 0;
};

} // namespace Engine
