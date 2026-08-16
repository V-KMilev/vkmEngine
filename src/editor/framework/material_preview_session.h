#pragma once

#include <cstdint>
#include <unordered_map>

#include "system/render/editor_render_hooks.h"

namespace Engine {

class RenderSystem;
class ResourceManager;

/**
 * @brief Editor-owned material preview cache over the backend's preview hooks.
 *
 * The backend (RenderBackend::renderPreview) does the actual offscreen studio
 * render; this class decides WHEN to render: it version-gates each key so an
 * unchanged asset never re-renders, and it budgets thumbnail bakes per frame
 * so opening a large Asset Browser grid spreads its renders over several
 * frames instead of stalling one. Live previews (the Material Editor pane)
 * bypass the budget - there is only ever one of those per frame.
 */
class MaterialPreviewSession {
    public:
        explicit MaterialPreviewSession(RenderSystem& renderSystem)
            : m_renderSystem(renderSystem) {}
        ~MaterialPreviewSession() = default;

        MaterialPreviewSession(const MaterialPreviewSession& other) = delete;
        MaterialPreviewSession& operator=(const MaterialPreviewSession& other) = delete;

        MaterialPreviewSession(MaterialPreviewSession && other) = delete;
        MaterialPreviewSession& operator=(MaterialPreviewSession && other) = delete;

    public:
        /**
         * @brief Live preview / cached thumbnail in one call.
         *
         * The caller fills @p req with what to draw (key, mesh, material,
         * orbit, background, light rotation); the session owns the output
         * size (live pane vs thumbnail). Returns the texture id of the
         * most-recent render for req.key, re-rendering only when @p version
         * differs from the cached stamp (and, for thumbnails, only within
         * this frame's bake budget). 0 = nothing to show yet.
         */
        uint32_t texture(
            ResourceManager& resources,
            PreviewRequest req,
            uint64_t version,
            bool live
        );

        /**
         * @brief Drop the cached render target for a single key.
         *
         * Call when the source asset is destroyed so its stale preview is not
         * served again.
         *
         * @param key Cache key whose target and version stamp are forgotten.
         */
        void evict(uint64_t key);

        /**
         * @brief Drop every cached render target.
         *
         * Used after a scene load swaps the asset set out from under the cache,
         * forcing every preview to re-render on next request.
         */
        void clear();

        /**
         * @brief Refill the per-frame thumbnail bake budget. Called by EditorSystem
         * at the top of each frame.
         */
        void onFrameBegin() { m_budget = THUMBS_PER_FRAME; }

    private:
        static constexpr int THUMBS_PER_FRAME = 2;

        static constexpr uint32_t LIVE_SIZE  = 512;  ///< Material Editor pane
        static constexpr uint32_t THUMB_SIZE = 256;  ///< Asset Browser tiles

        RenderSystem& m_renderSystem;

        /**
         * @brief key -> version stamp of the last render, so unchanged assets skip.
         */
        std::unordered_map<uint64_t, uint64_t> m_versions;

        int m_budget = 0;
};

} // namespace Engine
