#pragma once

#include <cstdint>
#include <string>

#include "core/reflect.h"

#include "resource/asset/mesh_asset.h"
#include "resource/asset/material_asset.h"
#include "system/render/render_backend.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief What fills the preview behind the mesh.
 */
enum class PreviewBackground : uint8_t {
    Dark,
    Grey,  ///< For judging albedo and silhouettes.
    Sky,   ///< The baked environment cubemap; Dark until the IBL bakes.

    Count  ///< Enum size marker (reflection); not a backdrop.
};

/**
 * @brief One editor preview: a mesh with a material under the scene's light, from an orbit camera.
 *
 * Rendering the same key again overwrites that key's cached target.
 */
struct PreviewRequest {
    uint64_t          key      = 0;      ///< Identifies the cached target.
    uint32_t          size     = 256;    ///< Square edge, pixels.
    MeshHandle        mesh;
    MaterialHandle    material;
    float             yawDeg   = 35.0f;
    float             pitchDeg = 20.0f;
    float             distance = 3.0f;   ///< In mesh bounding radii.
    PreviewBackground background = PreviewBackground::Dark;
    float             lightYawDeg = 0.0f;  ///< The light's rotation around Y, degrees.
};

/**
 * @brief A GPU texture, as an opaque id the UI layer can hand back to the backend.
 *
 * Only the backend interprets it; others pass it back or test it against zero
 * ("no texture"). 64 bits so a Vulkan or D3D12 descriptor handle fits.
 */
using GpuTextureId = uint64_t;

/**
 * @brief What a backend may offer an authoring tool beyond drawing the frame.
 *
 * Asset thumbnails, GPU mirrors of textures, and the host chrome's images - none
 * of which a shipped game asks for. Here, not in the editor, because the backend
 * implements it. A backend opts in by inheriting this and overriding
 * RenderBackend::editorHooks().
 */
class EditorRenderHooks {
    public:
        EditorRenderHooks() = default;
        virtual ~EditorRenderHooks() = default;

        EditorRenderHooks(const EditorRenderHooks& other) = delete;
        EditorRenderHooks& operator=(const EditorRenderHooks& other) = delete;

        EditorRenderHooks(EditorRenderHooks && other) = delete;
        EditorRenderHooks& operator=(EditorRenderHooks && other) = delete;

    public:
        /**
         * @brief Draw @p request offscreen and return the texture to display.
         *
         * @param request   What to draw.
         * @param resources Resolves the request's handles.
         * @return The rendered preview, or 0 when it could not draw one.
         */
        virtual GpuTextureId renderPreview(
            const PreviewRequest& request,
            const ResourceManager& resources
        ) = 0;

        /**
         * @brief A digest of what a preview borrows from the scene: its light and grade.
         *
         * A preview drawn under another digest is stale, though its asset is not.
         *
         * @return The digest; it changes when the light, sky strength, tonemap or exposure does.
         */
        virtual uint64_t previewLook() const = 0;

        /**
         * @brief The texture last rendered for @p key, without rendering one.
         *
         * @param key The request key.
         * @return That preview, or 0 if there is none.
         */
        virtual GpuTextureId previewTexture(uint64_t key) const = 0;

        /**
         * @brief Drop the cached target held for @p key.
         *
         * @param key An unknown key is a no-op.
         */
        virtual void releasePreview(uint64_t key) = 0;

        /**
         * @brief Drop every cached preview target.
         *
         * For when the asset graph is replaced and a handle-derived key means another asset.
         */
        virtual void releaseAllPreviews() = 0;

        /**
         * @brief GPU texture id already mirrored for @p handle, or 0 if none is.
         *
         * Never uploads. A texture is mirrored when a material binding it draws,
         * so 0 may mean "nothing has drawn it", which never resolves alone.
         *
         * @param handle An empty handle answers 0.
         * @return The mirror, or 0.
         */
        virtual GpuTextureId textureId(const TextureHandle& handle) const = 0;

        /**
         * @brief Mirror @p handle onto the GPU if it is not already, and return it.
         *
         * Idempotent and version-gated; the first call per texture pays a full
         * upload, so a caller showing many should spread them over frames.
         *
         * @param handle An empty handle answers 0.
         * @param resources Resolves the handle to its pixels.
         * @return The mirror, or 0 when the asset has no pixels yet (failed or in-flight decode).
         */
        virtual GpuTextureId ensureTexture(const TextureHandle& handle, const ResourceManager& resources) = 0;

        /**
         * @brief Upload an image the *engine* owns and return its texture id.
         *
         * For host chrome, which is not a project asset and must not go through
         * the ResourceManager. Cached by path. Pixels are bottom-up: a drawer with
         * top-left UVs flips them.
         *
         * @param path Absolute path to an image on disk.
         * @return Its id, or 0 when it could not be decoded.
         */
        virtual GpuTextureId chromeImage(const std::string& path) = 0;
};

/**
 * @brief The backend's editor hooks, or null when it offers none.
 *
 * @param backend Active backend; may be null.
 * @return The hooks, or null.
 */
inline EditorRenderHooks* editorRenderHooks(RenderBackend* backend) {
    return backend ? backend->editorHooks() : nullptr;
}

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::PreviewBackground, "Dark", "Grey", "Sky")

