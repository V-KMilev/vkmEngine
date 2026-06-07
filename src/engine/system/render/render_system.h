#pragma once

#include <functional>
#include <memory>
#include <cstdint>
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

        /**
         * @brief Two-step frame: extract a RenderView from the live world,
         *        then draw it.
         *
         * buildView() fills m_view from the live Scene, ResourceManager, and
         * Visibility. executeFrame() runs the backend sync + render graph
         * against it. The engine calls them back-to-back on the main thread;
         * keeping them separate leaves a clean seam if a render thread is
         * ever reintroduced (build on main, execute on the worker).
         */
        void buildView(FrameContext& ctx);
        void executeFrame(FrameContext& ctx);

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
        /// visualizer). Const so panels can't mutate the schedule; to
        /// toggle a pass use setPassEnabled().
        const RenderGraph& getGraph() const { return m_graph; }

        /// Mutable graph access for driving an offscreen render through the
        /// engine pipeline - the Material Editor / Asset Browser previews
        /// push their own FrameResources + target, execute, then pop. Not a
        /// license to edit the schedule; pass toggles go through
        /// setPassEnabled() or the view's environment flags.
        RenderGraph& getGraph() { return m_graph; }

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
         * @brief Re-bake the image-based-lighting environment next frame.
         *
         * Editor-facing "Rebake IBL": queues a backend job (runs at the top of
         * the next executeFrame, before the IBL bake pass) that invalidates the
         * cached bake so it re-runs from the current EnvironmentConfig::ibl.path.
         * Use after the .hdr changed on disk, or to force a refresh.
         */
        void requestIBLRebake();

        /**
         * @brief Queue a callable to run at the top of the next executeFrame(),
         *        before the scene render.
         *
         * Lets code that needs backend (GL) work run at a defined point in the
         * frame even when invoked from a later stage. The editor uses it for
         * material previews and IBL re-bake, requested during its UI update
         * (which runs after the render stage), so the work lands at the start of
         * the next frame - before that frame's scene render samples the result.
         *
         * Jobs run in queue order, are one-shot, and are dropped after running.
         * Callers capture any dependencies they need; references must outlive
         * the job (typically Engine-owned state which lives forever).
         */
        void queueBackendJob(std::function<void()> job);

    private:
        std::unique_ptr<RenderBackend> m_backend;
        RenderGraph m_graph;

        /// The frame's extracted RenderView. buildView() fills it, executeFrame()
        /// draws it; both run on the main thread back-to-back, so a single
        /// buffer suffices. Its vectors keep capacity across frames.
        RenderView m_view;

        /// Persistent shadow-caster cache (the sorted caster set survives across
        /// frames; only the matrices refresh). Written in buildView().
        ShadowCasterCache m_shadowCache;

        /// Queue of one-shot backend jobs. queueBackendJob() appends; the top of
        /// executeFrame() drains and runs them before the scene render.
        std::vector<std::function<void()>> m_pendingBackendJobs;

        EnvironmentConfig m_environment;

        uint32_t m_width  = 0;
        uint32_t m_height = 0;
};

} // namespace Engine
