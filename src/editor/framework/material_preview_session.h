#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "resource/material_asset.h"
#include "resource/mesh_asset.h"
#include "system/render/render_view.h"

namespace Engine {

class FrameResources;
class RenderSystem;
class RenderTarget;
class ResourceManager;

/**
 * @brief Editor-owned material preview renderer.
 *
 * Routes the editor's Material Editor + Asset Browser previews through
 * the engine's render graph without forcing the engine to own any
 * preview-specific state. Holds per-key offscreen render targets keyed
 * by the caller's asset id - one allocation per cached thumbnail; no
 * separate snapshot/copy step.
 *
 * On every render() the session swaps its own FrameResources + target
 * into the graph for the duration of one execute, then restores the
 * default ones. Studio camera + key light + the scene's environment
 * (with view-dependent / temporal effects forced off) give every
 * material a stable, identical lighting setup.
 *
 * Lifetime: one instance per EditorSystem; constructed at editor boot
 * after the engine has set its backend.
 */
class MaterialPreviewSession {
    public:
        MaterialPreviewSession(RenderSystem& renderSystem, uint32_t size);
        ~MaterialPreviewSession();

        MaterialPreviewSession(const MaterialPreviewSession& other) = delete;
        MaterialPreviewSession& operator=(const MaterialPreviewSession& other) = delete;

        MaterialPreviewSession(MaterialPreviewSession && other) = delete;
        MaterialPreviewSession& operator=(MaterialPreviewSession && other) = delete;

    public:
        /**
         * @brief Live preview / cached thumbnail in one call.
         *
         * @p live = true forces an immediate render this frame (Material
         * Editor's main panel - always up to date). @p live = false uses
         * the per-frame bake budget + the @p version check so the
         * Asset Browser grid amortises rebuilds across many frames.
         *
         * Returns the texture id of the most-recent render for @p key,
         * or 0 if nothing has been rendered yet.
         */
        uint32_t texture(
            ResourceManager& resources,
            const MaterialHandle& material,
            const MeshHandle& mesh,
            float yawDeg,
            float pitchDeg,
            float distance,
            uint64_t key,
            uint64_t version,
            bool live
        );

        /// Drop the per-key target. Call when the source asset is destroyed.
        void evict(uint64_t key);

        /// Drop every cached target.
        void clear();

        /**
         * @brief Refill the per-frame thumbnail bake budget. Called by
         *        EditorSystem at the top of each frame.
         */
        void onFrameBegin();

    private:
        /// Render path. Runs on the render thread only, since it allocates
        /// GL resources (target) and issues draw calls. Looks up or
        /// creates the per-key target before driving the graph.
        void renderOnBackendThread(
            ResourceManager& resources,
            const MaterialHandle& material,
            const MeshHandle& mesh,
            float yawDeg,
            float pitchDeg,
            float distance,
            uint64_t key
        );

        /// Current texture id for @p key under the targets mutex (0 if
        /// nothing has been rendered yet). Main thread calls this to
        /// hand a stable id to ImGui::Image.
        uint32_t cachedTextureId(uint64_t key) const;

        RenderSystem& m_renderSystem;
        uint32_t      m_size;

        /// Shared frame resources at the preview's fixed resolution.
        /// Lazily created on the first render-thread render so allocation
        /// happens against the backend's actual context, not main.
        std::unique_ptr<FrameResources> m_frame;

        /// Per-key offscreen targets + cached color texture ids. The map
        /// is mutated only on the render thread; the id map is read on
        /// main (ImGui draw) and written on render - mutex guards both.
        mutable std::mutex                                          m_targetsMutex;
        std::unordered_map<uint64_t, std::unique_ptr<RenderTarget>> m_targets;
        std::unordered_map<uint64_t, uint32_t>                      m_textureIds;

        /// Last @p version we baked for each key (only tracked when
        /// live == false; the live path bakes every frame). Touched on
        /// the main thread alongside the texture() lookup.
        std::unordered_map<uint64_t, uint64_t> m_versions;

        /// Scratch RenderView reused across renders - keeps drawables /
        /// lights vector capacity.
        RenderView m_view;

        /// Passes we forcibly disabled around a preview (grid, AABB)
        /// and their prior enabled state. Restored after the render so
        /// the main viewport's pipeline is unchanged.
        std::vector<std::pair<size_t, bool>> m_passWasEnabled;

        /// Thumbnail throttle: at most BUDGET_PER_FRAME fresh
        /// (non-live) bakes per frame so an Asset Browser grid spreads
        /// its work out.
        static constexpr uint32_t BUDGET_PER_FRAME = 3;
        uint32_t m_budget = 0;
};

} // namespace Engine
