#pragma once

#include <cstdint>
#include <string>

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
 * @brief What a backend may offer an authoring tool beyond drawing the frame.
 *
 * Two things, and they are here for the same reason: an offscreen render of one
 * asset (the thumbnail behind every material and mesh in the browser), and read
 * access to the GPU mirror of a texture the frame path uploaded (or an upload of
 * one it never had reason to).
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

        /**
         * @brief The texture last rendered for @p key, without rendering one.
         *
         * What a panel drawing an already-requested preview reads each frame.
         *
         * @param key The request key the preview was rendered under.
         * @return The backend's id for that preview, or 0 if there is none.
         */
        virtual GpuTextureId previewTexture(uint64_t key) const = 0;

        /**
         * @brief Drop the cached target held for @p key.
         *
         * @param key The request key to forget; an unknown key is a no-op.
         */
        virtual void releasePreview(uint64_t key) = 0;

        /**
         * @brief Drop every cached preview target.
         *
         * What a project close calls: the keys are derived from asset handles,
         * and the next project's handles mean different assets.
         */
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

        /**
         * @brief Upload an image the *engine* owns and return its texture id.
         *
         * The editor's own chrome - the brand mark in the menu bar - is not a
         * project asset. Routed through the ResourceManager it would land in the
         * user's asset library as an unused import and go stale every time
         * opening a project swaps that manager. So it comes through here, which
         * is the seam an authoring tool already reaches the GPU by, instead of
         * the editor constructing a GL texture of its own.
         *
         * Cached by path; a repeat call re-uses the upload. The pixels arrive
         * bottom-up, as every decode in the engine does, so a drawer whose UVs
         * run from the top left flips them at the draw.
         *
         * @param path Absolute path to an image on disk.
         * @return The backend's id for it, or 0 when it could not be decoded.
         */
        virtual GpuTextureId chromeImage(const std::string& path) = 0;
};

/**
 * @brief The backend's editor hooks, or null when it offers none.
 *
 * @param backend Active backend; null is answered with null.
 */
inline EditorRenderHooks* editorRenderHooks(RenderBackend* backend) {
    return backend ? backend->editorHooks() : nullptr;
}

} // namespace Vkm::Engine
