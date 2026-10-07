#pragma once

#include <cstdint>
#include <unordered_map>

#include "system/render/editor_render_hooks.h"

namespace Vkm::Engine {

class RenderSystem;
class ResourceManager;

/**
 * @brief Editor-owned material preview cache over the backend's preview hooks.
 *
 * The backend's EditorRenderHooks::renderPreview does the actual offscreen
 * render; this class decides WHEN to render: it version-gates each key so an
 * unchanged asset never re-renders, and it budgets thumbnail bakes per frame
 * so a large grid of thumbnails spreads its renders over several frames
 * instead of stalling one. Live previews bypass the budget.
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
         * size. It re-renders only when @p version or the scene's look
         * (EditorRenderHooks::previewLook) differs from the cached stamp.
         *
         * @param resources Resolves the request's mesh and material.
         * @param req       What to draw; its size is the session's to set.
         * @param version   The asset's version stamp; an unchanged one reuses
         *                  the cached render.
         * @param live      True for a live preview, which skips the per-frame
         *                  thumbnail budget and renders at LIVE_SIZE.
         * @return The texture of the most recent render for req.key, or 0 when
         *         there is nothing to show yet.
         */
        uint32_t texture(ResourceManager& resources, PreviewRequest req, uint64_t version, bool live);

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
         * For a scene load that swaps the asset set out from under the cache.
         */
        void clear();

        /**
         * @brief Refill the per-frame thumbnail bake budget.
         */
        void onFrameBegin() { m_budget = THUMBS_PER_FRAME; }

    private:
        static constexpr int THUMBS_PER_FRAME = 2;

        static constexpr uint32_t LIVE_SIZE  = 512;  ///< A live preview's edge, in pixels
        static constexpr uint32_t THUMB_SIZE = 256;  ///< A thumbnail's edge, in pixels

    private:
        RenderSystem& m_renderSystem;

        /**
         * @brief key -> version stamp of the last render, so unchanged assets skip.
         */
        std::unordered_map<uint64_t, uint64_t> m_versions;

        int m_budget = 0;
};

} // namespace Vkm::Engine
