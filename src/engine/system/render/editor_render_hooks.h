#pragma once

#include <cstdint>

#include "resource/asset/mesh_asset.h"
#include "resource/asset/material_asset.h"
#include "system/render/render_backend.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief What fills the preview behind the mesh.
 */
enum class PreviewBackground : uint8_t {
    Dark,  ///< Dark studio backdrop (the default).
    Grey,  ///< Mid-grey backdrop, for judging albedo and silhouettes.
    Sky,   ///< The baked environment cubemap (falls back to Dark before the IBL bakes).
};

/**
 * @brief One editor preview render: draw a mesh with a material under a studio
 *        light rig, from an orbit camera, into a per-key target.
 *
 * The key identifies the cached output texture across frames (the editor
 * derives it from the asset handle); rendering the same key again overwrites
 * that target. Sizes are square pixels.
 */
struct PreviewRequest {
    uint64_t          key      = 0;      ///< Identifies the cached target.
    uint32_t          size     = 256;    ///< Output edge length in pixels.
    MeshHandle        mesh;              ///< Shape to draw.
    MaterialHandle    material;          ///< Material to draw it with.
    float             yawDeg   = 35.0f;  ///< Orbit yaw (degrees).
    float             pitchDeg = 20.0f;  ///< Orbit pitch (degrees).
    float             distance = 3.0f;   ///< Camera distance, in mesh bounding radii.
    PreviewBackground background = PreviewBackground::Dark;  ///< Backdrop behind the mesh.
    float             lightYawDeg = 0.0f;  ///< Studio rig rotation around Y (degrees).
};

/**
 * @brief A GPU texture, as an opaque id the UI layer can hand back to the backend.
 *
 * The value means whatever the backend wants it to: OpenGL returns the GL
 * texture name, and a Vulkan or D3D12 backend would return a descriptor handle.
 * Nothing outside the backend may interpret it - the only valid operations are
 * passing it back and testing it against zero, which always means "no texture".
 *
 * 64 bits because that is what the wider APIs need. GL names fit in 32, but
 * VkDescriptorSet and a D3D12 descriptor handle do not, and a type that cannot
 * represent the second backend's handles is not an abstraction.
 */
using GpuTextureId = uint64_t;

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

} // namespace Vkm::Engine
