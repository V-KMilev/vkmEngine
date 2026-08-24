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
 * 64 bits because GL names fit in 32 but VkDescriptorSet and a D3D12 descriptor
 * handle do not.
 */
using GpuTextureId = uint64_t;

/**
 * @brief Offscreen rendering a backend may offer for authoring tools.
 *
 * Separate from RenderBackend on purpose: drawing a frame is what a backend is
 * *for*, while an asset thumbnail is an authoring convenience a shipped game
 * never asks for.
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
         * @brief GPU texture id already mirrored for @p handle, or 0 if none is.
         *
         * Reports; it never uploads. A texture is mirrored as a side effect of
         * drawing the material that binds it, so 0 means "still decoding" or
         * "nothing has drawn this yet", and the second never resolves alone.
         *
         * @param handle Texture to look up; an empty handle answers 0.
         * @return The backend's id for the mirror, or 0.
         */
        virtual GpuTextureId textureId(const TextureHandle& handle) const = 0;

        /**
         * @brief Mirror @p handle onto the GPU if it is not already, and return it.
         *
         * What a library browser needs and textureId() cannot give it: the
         * textures no material on screen binds never get a mirror otherwise.
         * Idempotent and version-gated, but the first call per texture pays a
         * full upload, so a caller showing many should spread them over frames.
         *
         * @param handle Texture to mirror; an empty handle answers 0.
         * @param resources Resolves the handle to the pixels to upload.
         * @return The backend's id for the mirror, or 0 when the asset has no
         *         pixels yet (failed decode, or one still in flight).
         */
        virtual GpuTextureId ensureTexture(const TextureHandle& handle,
                                           const ResourceManager& resources) = 0;
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
