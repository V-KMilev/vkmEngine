#pragma once

#include <cstdint>

#include "system/render/render_backend.h"

namespace Engine {

class ResourceManager;

/**
 * @brief Offscreen rendering a backend may offer for authoring tools.
 *
 * Separate from RenderBackend on purpose. Drawing a frame is what a backend is
 * *for*; rendering an asset thumbnail is a convenience for whoever is editing
 * the scene, and a shipped game never asks for one. Folding these into the
 * runtime interface meant every backend, and every runtime build, carried the
 * concept of a material preview - the one place the engine's otherwise strict
 * layering bent inward.
 *
 * It lives under system/render rather than in the editor because the backend
 * has to implement it and cannot see editor code. Nothing in the frame path
 * refers to it.
 *
 * A backend opts in by also inheriting this; the editor asks for it with
 * editorRenderHooks() and shows placeholders when the answer is null.
 */
class EditorRenderHooks {
    public:
        virtual ~EditorRenderHooks() = default;

        /**
         * @brief Draw @p request offscreen and return the texture to display.
         *
         * The request's key identifies the cached output across frames;
         * rendering the same key again overwrites that target.
         *
         * @param request   What to draw, at what size, under which key.
         * @param resources Resolves the request's handles.
         */
        virtual GpuTextureId renderPreview(const PreviewRequest& request,
                                           const ResourceManager& resources) = 0;

        /// Last texture rendered for @p key, or 0 if there is none.
        virtual GpuTextureId previewTexture(uint64_t key) const = 0;

        virtual void releasePreview(uint64_t key) = 0;
        virtual void releaseAllPreviews() = 0;

        /**
         * @brief GPU texture id for @p handle - editor thumbnails.
         *
         * 0 while the texture has no GPU mirror yet (never drawn, or still
         * decoding); the editor shows a placeholder and retries next frame.
         */
        virtual GpuTextureId textureId(const TextureHandle& handle) const = 0;
};

/**
 * @brief The backend's editor hooks, or null when it offers none.
 *
 * A cast rather than a method on RenderBackend, so the runtime interface does
 * not have to name this one.
 *
 * @param backend Active backend; null is answered with null.
 */
inline EditorRenderHooks* editorRenderHooks(RenderBackend* backend) {
    return dynamic_cast<EditorRenderHooks*>(backend);
}

} // namespace Engine
