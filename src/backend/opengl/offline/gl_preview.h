#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "gl_frame_buffer.h"
#include "gl_screen_triangle.h"

#include "gl_target.h"
#include "frame/gl_camera.h"
#include "frame/gl_lights.h"
#include "frame/gl_instance_batcher.h"
#include "frame/gl_object_buffer.h"
#include "system/render/data/render_objects.h"

namespace Vkm::GL {
    class Context;
    class Shader;
    class Texture2D;
}

namespace Vkm::Engine {

class GLView;
class GLIBL;
class GLSceneCapture;
class GLShadowAtlas;
class ResourceManager;
struct PreviewRequest;

/**
 * @brief Renders editor material/mesh previews offscreen.
 *
 * A request is one (mesh, material) pair under a three-light studio rig and the global IBL,
 * drawn from an orbit camera into a shared HDR scratch, then tonemapped into a per-key LDR
 * texture. It rebinds the camera UBO and lights SSBO, so it must run outside the frame's passes.
 * The geometry and sky programs are the backend's, borrowed; the tonemap program and scratch are
 * built on the first request, so a host that never previews pays nothing.
 */
class GLPreview {
    public:
        /**
         * @brief Draw through the backend's shared offline rig.
         *
         * @param capture Owns the PBR and skybox programs and the sky cube; outlives this preview.
         */
        explicit GLPreview(GLSceneCapture& capture);
        ~GLPreview();

        GLPreview(const GLPreview& other) = delete;
        GLPreview& operator=(const GLPreview& other) = delete;

        GLPreview(GLPreview && other) = delete;
        GLPreview& operator=(GLPreview && other) = delete;

    public:
        /**
         * @brief Render @p req into its per-key target.
         *
         * @param gl        Live GL context the preview draws through.
         * @param glView    GPU mirror the mesh and material resolve against.
         * @param ibl       The environment the studio is lit by.
         * @param shadows   The atlas, bound for the shader's sake; a preview casts no shadow.
         * @param req       What to draw, at what size, under which key.
         * @param resources Resolves the request's handles.
         * @return The LDR texture id, or 0 when assets are missing.
         */
        uint32_t render(
            Vkm::GL::Context& gl,
            GLView& glView,
            const GLIBL& ibl,
            const GLShadowAtlas& shadows,
            const PreviewRequest& req,
            const ResourceManager& resources
        );

        /**
         * @brief The texture last rendered for @p key.
         *
         * @param key The request key.
         * @return Its texture id, or 0 when none exists.
         */
        uint32_t texture(uint64_t key) const;

        /**
         * @brief Drop one key's target.
         *
         * For when the source asset is destroyed.
         *
         * @param key The request key to forget; an unknown key is a no-op.
         */
        void release(uint64_t key);

        /**
         * @brief Drop every cached target.
         */
        void releaseAll();

    private:
        /**
         * @brief Per-key output: an LDR texture in its own FBO, sized per request.
         */
        struct Entry {
            std::unique_ptr<Vkm::GL::Texture2D> ldr;
            Vkm::GL::FrameBuffer                fbo;
            uint32_t                            size = 0;
        };

        Entry& ensureEntry(uint64_t key, uint32_t size);

        /**
         * @brief Compile the tonemap program and create the scratch target.
         *
         * Called by the first render(), which is the only place a live GL
         * context is guaranteed and the only proof the rig is wanted.
         */
        void init();

    private:
        GLSceneCapture& m_capture;  ///< The shared offline rig: PBR, skybox, sky cube.

        std::unique_ptr<Vkm::GL::Shader> m_composite;  ///< Tonemap HDR -> LDR.
        std::unique_ptr<ScreenTriangle>  m_tri;        ///< Fullscreen tonemap draw.

        /**
         * @brief Shared HDR scene target, at a fixed size.
         *
         * Depth, no G-buffer: a preview runs no screen-space passes.
         */
        GLTarget          m_scratch{GLTarget::Layout::ColorDepth};
        GLCamera          m_camera;    ///< Orbit camera UBO.
        GLLights          m_lights;    ///< Studio rig lights SSBO.
        GLInstanceBatcher m_batcher;   ///< Single-object instanced draw.

        /**
         * @brief The one object a request draws, at the origin, and its transform buffer.
         *
         * The preview's own: it draws between frames, and the frame's buffer holds the scene's.
         */
        RenderObjects  m_object;
        GLObjectBuffer m_objectBuffer;

        std::unordered_map<uint64_t, std::unique_ptr<Entry>> m_entries;
};

} // namespace Vkm::Engine
