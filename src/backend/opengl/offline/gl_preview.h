#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "vkm_opengl/target/frame_buffer.h"

#include "gl_screen_triangle.h"

#include "storage/gl_target.h"
#include "frame/gl_camera.h"
#include "frame/light/gl_lights.h"
#include "frame/draw/gl_instance_batcher.h"
#include "frame/gl_object_buffer.h"
#include "frame/gl_skin_palette.h"
#include "system/render/data/light_data.h"
#include "core/frame/render_objects.h"
#include "io/project/render_settings.h"

namespace Vkm::OpenGL {
    class Context;
    class Shader;
    class Texture2D;
}

namespace Vkm::Engine {

class GLAssetMirror;
class GLIBL;
class GLSceneCapture;
class GLShadowAtlas;
class ResourceManager;
struct PreviewRequest;

/**
 * @brief What a preview borrows from the scene, so a material looks as it will in it.
 *
 * Before any frame, and in a scene with no directional light, a preview takes the studio rig.
 */
struct PreviewScene {
    std::optional<LightData> sun;                                       ///< The key light, shadowless.
    float                    skyIntensity = 1.0f;                       ///< Environment's sky.intensity.
    Tonemap                  tonemap      = RenderSettings{}.tonemap;
    float                    exposure     = RenderSettings{}.exposure;  ///< In stops.
};

/**
 * @brief Renders editor previews offscreen: a material on a shape, a mesh, a model's meshes.
 *
 * A request's meshes are drawn under the scene's key light (the studio rig where it has none),
 * its sky and its tonemap, from an orbit camera around them, into an HDR scene target, then
 * tonemapped into a per-key LDR texture. A square thumbnail shares one scene target; any other
 * size, a viewport's, keeps its own. It rebinds the camera UBO and lights SSBO, so it must run
 * outside the frame's passes. The geometry and sky programs are the backend's, borrowed; the
 * tonemap program and shared target are built on the first request, so a host that never
 * previews pays nothing.
 */
class GLPreview {
    public:
        /**
         * @brief Draw through the backend's shared offline rig.
         *
         * @param capture Draws the sky with its skybox program and the backend's cube, and lends the
         *                forward programs it borrows from the backend; outlives this preview.
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
         * @param mirror    GPU mirror the mesh and material resolve against.
         * @param ibl       The environment the preview is lit by.
         * @param scene     The sun, sky strength, tonemap and exposure the scene renders by.
         * @param shadows   The atlas, bound for the shader's sake; a preview casts no shadow.
         * @param req       What to draw, at what size, under which key.
         * @param resources Resolves the request's handles.
         * @return The LDR texture id, or 0 when assets are missing.
         */
        uint32_t render(
            Vkm::OpenGL::Context& gl,
            GLAssetMirror& mirror,
            const GLIBL& ibl,
            const PreviewScene& scene,
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
         * For when the source asset is destroyed, or its window closed. This frame's UI may already
         * have drawn its texture, so it is freed by the next freeReleased(), not here.
         *
         * @param key The request key to forget; an unknown key is a no-op.
         */
        void release(uint64_t key);

        /**
         * @brief Drop every cached target, as release() does.
         */
        void releaseAll();

        /// Free what was released; called once the frame's UI has drawn, as the next one starts.
        void freeReleased() { m_released.clear(); }

    private:
        /**
         * @brief Per-key output: an LDR texture in its own FBO, sized per request, and the scene
         *        target of a request that is no square thumbnail.
         */
        struct Entry {
            std::unique_ptr<Vkm::OpenGL::Texture2D> ldr;
            Vkm::OpenGL::FrameBuffer                fbo;
            uint32_t                                width  = 0;
            uint32_t                                height = 0;
            std::unique_ptr<GLTarget>               scene;
        };

        /**
         * @brief The entry for @p key, its texture sized @p width by @p height.
         *
         * @param key    The request key.
         * @param width  Pixels.
         * @param height Pixels.
         * @return The entry, made or resized as needed.
         */
        Entry& ensureEntry(uint64_t key, uint32_t width, uint32_t height);

        /**
         * @brief Compile the tonemap program and create the scratch target.
         *
         * Called by the first render(), which is the only place a live GL
         * context is guaranteed and the only proof the rig is wanted.
         */
        void init();

    private:
        GLSceneCapture& m_capture;  ///< The shared offline rig: PBR, skybox, sky cube.

        std::unique_ptr<Vkm::OpenGL::Shader> m_composite;  ///< Tonemap HDR -> LDR.
        std::unique_ptr<GLScreenTriangle>    m_tri;        ///< Fullscreen tonemap draw.

        GLTarget          m_scratch{GLTarget::Layout::ColorDepth};
            ///< The square thumbnails' shared HDR scene target, at a fixed size. Depth, no G-buffer: a
            ///< preview runs no screen-space passes.
        GLCamera          m_camera;   ///< Orbit camera UBO.
        GLLights          m_lights;   ///< Studio rig lights SSBO.
        GLInstanceBatcher m_batcher;  ///< The request's instanced draws.

        RenderObjects  m_object;  ///< The objects a request draws and their transform buffer. The preview's
                                  ///< own: it draws between frames, and the frame's buffer holds the scene's.
        GLObjectBuffer m_objectBuffer;

        std::vector<glm::mat4> m_bones;  ///< The request's palettes end to end, as skinFirst addresses them.
        GLSkinPalette          m_skin;   ///< They, in the buffer the skinned programs read.

        std::vector<uint32_t> m_opaque;       ///< The request's objects drawn first.
        std::vector<uint32_t> m_transparent;  ///< Then those blended over them.

        std::unordered_map<uint64_t, std::unique_ptr<Entry>> m_entries;
        std::vector<std::unique_ptr<Entry>>                  m_released;  ///< Until the UI drew them.
};

} // namespace Vkm::Engine
