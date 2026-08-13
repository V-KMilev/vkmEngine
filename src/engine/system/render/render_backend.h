#pragma once

#include <cstdint>
#include <string>

#include "resource/asset/mesh_asset.h"
#include "resource/asset/material_asset.h"

namespace Engine {
    struct RenderView;
    class ResourceManager;
    class WindowManager;
}

namespace Engine {

/**
 * @brief Which graphics API a backend speaks.
 *
 * Tags the active backend; the human-readable identity is reported through info().
 */
enum class RenderBackendType {
    OpenGL,  ///< OpenGL-based rendering backend.
};

/**
 * @brief Human-readable backend identity for the editor status bar.
 *
 * Deliberately generic: swapping backends changes the strings, never the call site.
 */
struct BackendInfo {
    std::string api;     ///< e.g. "OpenGL 4.6"
    std::string device;  ///< e.g. "NVIDIA GeForce RTX 3080"
};

/**
 * @brief One editor preview render: draw @p mesh with @p material under a
 *        studio light rig, from an orbit camera, into a per-@p key target.
 *
 * The key identifies the cached output texture across frames (the editor
 * derives it from the asset handle); rendering the same key again overwrites
 * that target. Sizes are square pixels.
 */
/**
 * @brief What fills the preview behind the mesh.
 */
enum class PreviewBackground : uint8_t {
    Dark,  ///< Dark studio backdrop (the default).
    Grey,  ///< Mid-grey backdrop, for judging albedo and silhouettes.
    Sky,   ///< The baked environment cubemap (falls back to Dark before the IBL bakes).
};

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
 * @brief The single seam between the engine and the graphics API.
 *
 * The engine hands the backend one POD RenderView per frame; the backend owns
 * everything below this line - the API context, GPU resource mirror, passes,
 * and presentation. Nothing above this interface holds an API object, so
 * swapping backends is just destroying one implementation and constructing
 * another: there is no GPU state to migrate, and the new backend re-uploads
 * what it needs from the handles in the next RenderView.
 */
class RenderBackend {
    public:
        explicit RenderBackend(RenderBackendType type) : m_type(type) {}
        virtual ~RenderBackend() = default;

        RenderBackend(const RenderBackend& other) = delete;
        RenderBackend& operator=(const RenderBackend& other) = delete;

        RenderBackend(RenderBackend && other) = delete;
        RenderBackend& operator=(RenderBackend && other) = delete;

    public:
        RenderBackendType type() const { return m_type; }
        BackendInfo info() const { return m_info; }

        /**
         * @brief Bring the backend up against the window.
         *
         * Acquires / makes-current the API context or surface and creates any
         * persistent GPU state. Returns false if the backend cannot run here,
         * so a failed runtime swap can keep the previous backend instead.
         */
        virtual bool init(WindowManager& window) = 0;

        /**
         * @brief Set the viewport rect that subsequent frames draw into.
         *
         * x/y are the origin inside the window (lets an editor render into a
         * sub-rect); width/height are its size.
         */
        virtual void resize(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;

        /**
         * @brief Draw and present one frame.
         *
         * @param view      Backend-agnostic snapshot of what to draw this frame.
         * @param resources Resolves the view's handles to asset data; the backend
         *                  mirrors that onto the GPU, uploading only what changed.
         */
        virtual void render(const RenderView& view, const ResourceManager& resources) = 0;

        /**
         * @brief Editor preview hooks - optional.
         *
         * renderPreview() draws the request offscreen and returns an opaque
         * texture id the UI layer can display (for OpenGL: the GL texture
         * name ImGui samples); previewTexture() returns the last-rendered
         * texture for a key, or 0 when none exists. A backend without an
         * offscreen path keeps these no-ops and the editor shows no image.
         */
        virtual uint32_t renderPreview(const PreviewRequest& request,
                                       const ResourceManager& resources) { return 0; }
        virtual uint32_t previewTexture(uint64_t key) const { return 0; }
        virtual void releasePreview(uint64_t key) {}
        virtual void releaseAllPreviews() {}

        /**
         * @brief GPU texture id for @p handle - editor thumbnails.
         *
         * Same opaque id family as renderPreview (for OpenGL: the GL texture
         * name ImGui samples). Returns 0 while the texture has no GPU mirror
         * yet (never drawn, or still decoding) - the editor shows a
         * placeholder and retries next frame.
         */
        virtual uint32_t textureId(const TextureHandle& handle) const { return 0; }

    protected:
        RenderBackendType m_type;
        BackendInfo m_info;
};

} // namespace Engine
