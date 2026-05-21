#pragma once

#include <memory>
#include <cstdint>
#include <vector>
#include <utility>
#include <unordered_map>

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

        RenderGraph& getGraph() { return m_graph; }
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
         * @brief Render one material on a preview shape through the REAL
         *        pipeline into an offscreen texture; returns its backend
         *        texture id (usable as an ImGui ImTextureID; 0 on failure).
         *
         * Editor-facing (Material Editor / Asset Browser). Builds a studio
         * RenderView (orbit camera + key light + the scene's environment) and
         * re-executes the same render graph in the backend's preview-target
         * mode - so the preview gets IBL/SSR/GTAO/bloom/tone mapping for free
         * and can never drift from the viewport.
         *
         * @param mesh The shape to draw (a built-in preview primitive or the
         *             selected entity's mesh - the caller decides; this layer
         *             stays free of the mesh generators).
         */
        uint32_t renderMaterialPreview(ResourceManager& resources,
                const MaterialHandle& material, const MeshHandle& mesh,
                float yawDeg, float pitchDeg, float distance, uint32_t size);

        /**
         * @brief Stable preview/thumbnail texture for one material on a shape.
         *
         * Wraps renderMaterialPreview with a per-@p key snapshot so the
         * result survives later previews overwriting the shared target
         * (Asset Browser grid + the live Material Editor).
         *
         * @param key  Caller-defined cache key (0 is fine; pick distinct keys
         *             per asset). @param version Re-bake only when this
         *             changes (e.g. the asset's resource version).
         * @param live true = always re-render this frame (Material Editor),
         *             bypassing the version check and the per-frame budget.
         *             false = budgeted lazy bake (thumbnail grid); returns the
         *             last snapshot (or 0) until its turn comes.
         */
        uint32_t materialPreviewTexture(ResourceManager& resources,
                const MaterialHandle& material, const MeshHandle& mesh,
                float yawDeg, float pitchDeg, float distance,
                uint64_t key, uint64_t version, bool live);

    private:
        /// Mirror env toggles onto pass.setEnabled() before execute, so the
        /// graph's resolve-dirty tracking only counts passes that actually run.
        void syncPassToggles();

        std::unique_ptr<RenderBackend> m_backend;
        RenderGraph m_graph;

        RenderView m_renderView;  ///< Persistent - vectors reuse capacity across frames.

        // Material-preview scratch (editor): persistent view to reuse capacity.
        RenderView m_previewView;
        std::vector<std::pair<size_t, bool>> m_previewPassWasEnabled;  ///< (pass index, prior enabled) restored after a preview

        // Thumbnail throttle: at most THUMB_BUDGET_PER_FRAME fresh (non-live)
        // bakes per frame so an Asset Browser grid spreads its work out.
        static constexpr uint32_t THUMB_BUDGET_PER_FRAME = 3;
        static constexpr uint32_t PREVIEW_RES            = 512;  ///< One fixed offscreen res for all previews (no target thrash)
        uint32_t m_thumbBudget = 0;
        std::unordered_map<uint64_t, uint64_t> m_thumbVersion;  ///< key -> last baked asset version

        EnvironmentConfig m_environment;

        uint32_t m_width  = 0;
        uint32_t m_height = 0;
};

} // namespace Engine
