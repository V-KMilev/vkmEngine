#pragma once

#include <cstdint>

#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"

namespace Engine {

class RenderSystem;
class ResourceManager;

/**
 * @brief Editor-owned material preview renderer - currently a stub.
 *
 * The previous implementation rendered thumbnails through the render graph,
 * which the render refactor removed. The editor-facing API is kept so the
 * Material Editor / Asset Browser call sites stay intact; texture() returns
 * 0 (no preview) until the backend grows an offscreen-preview path again.
 */
class MaterialPreviewSession {
    public:
        MaterialPreviewSession(RenderSystem& renderSystem, uint32_t size)
            : m_renderSystem(renderSystem), m_size(size) {}
        ~MaterialPreviewSession() = default;

        MaterialPreviewSession(const MaterialPreviewSession& other) = delete;
        MaterialPreviewSession& operator=(const MaterialPreviewSession& other) = delete;

        MaterialPreviewSession(MaterialPreviewSession && other) = delete;
        MaterialPreviewSession& operator=(MaterialPreviewSession && other) = delete;

    public:
        /// Live preview / cached thumbnail in one call. Returns the texture id
        /// of the most-recent render for @p key, or 0 if nothing has been
        /// rendered yet (always 0 while the preview path is stubbed out).
        uint32_t texture(
            ResourceManager& /*resources*/,
            const MaterialHandle& /*material*/,
            const MeshHandle& /*mesh*/,
            float /*yawDeg*/,
            float /*pitchDeg*/,
            float /*distance*/,
            uint64_t /*key*/,
            uint64_t /*version*/,
            bool /*live*/
        ) { return 0u; }

        /// Drop the per-key target. Call when the source asset is destroyed.
        void evict(uint64_t /*key*/) {}

        /// Drop every cached target.
        void clear() {}

        /// Refill the per-frame thumbnail bake budget. Called by EditorSystem
        /// at the top of each frame.
        void onFrameBegin() {}

    private:
        [[maybe_unused]] RenderSystem& m_renderSystem;
        [[maybe_unused]] uint32_t      m_size;
};

} // namespace Engine
