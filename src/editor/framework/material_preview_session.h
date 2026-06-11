#pragma once

#include <cstdint>
#include <unordered_map>

#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"

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
         * Returns the texture id of the most-recent render for @p key,
         * re-rendering only when @p version differs from the cached stamp
         * (and, for thumbnails, only within this frame's bake budget).
         * 0 = nothing to show yet.
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

        /// Drop every cached target (e.g. after a scene load swapped assets).
        void clear();

        /// Refill the per-frame thumbnail bake budget. Called by EditorSystem
        /// at the top of each frame.
        void onFrameBegin() { m_budget = THUMBS_PER_FRAME; }

    private:
        static constexpr int THUMBS_PER_FRAME = 2;

        static constexpr uint32_t LIVE_SIZE  = 512;  ///< Material Editor pane
        static constexpr uint32_t THUMB_SIZE = 256;  ///< Asset Browser tiles

        RenderSystem& m_renderSystem;

        /// key -> version stamp of the last render, so unchanged assets skip.
        std::unordered_map<uint64_t, uint64_t> m_versions;

        int m_budget = 0;
};

} // namespace Engine
