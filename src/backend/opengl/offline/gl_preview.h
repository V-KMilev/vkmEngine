#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "gl_frame_buffer.h"
#include "gl_screen_triangle.h"

#include "gl_target.h"
#include "frame/gl_camera.h"
#include "frame/gl_ocean.h"
#include "frame/gl_lights.h"
#include "frame/gl_instance_batcher.h"
#include "frame/gl_object_buffer.h"
#include "frame/gl_skin_palette.h"
#include "system/render/data/light_data.h"
#include "system/render/data/render_objects.h"
#include "system/render/render_settings.h"

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
 * @brief What a preview borrows from the scene, so a material looks as it will in it.
 *
 * Before any frame, and in a scene with no directional light, a preview takes the studio rig.
 */
struct PreviewScene {
    std::optional<LightData> sun;                                ///< The key light, shadowless.
    float                    skyIntensity = 1.0f;                ///< Environment's sky.intensity.
    Tonemap                  tonemap      = RenderSettings{}.tonemap;
    float                    exposure     = RenderSettings{}.exposure;  ///< In stops.
};

/**
 * @brief Renders editor previews offscreen: a material on a shape, a mesh, a model's meshes.
 *
 * A request's meshes are drawn under the scene's key light (the studio rig where it has none),
 * its sky and its grade, from an orbit camera around them, into an HDR scene target, then
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
         * @param glView    GPU mirror the mesh and material resolve against.
         * @param ibl       The environment the preview is lit by.
         * @param scene     The sun, sky strength, tonemap and exposure the scene renders by.
         * @param shadows   The atlas, bound for the shader's sake; a preview casts no shadow.
         * @param req       What to draw, at what size, under which key.
         * @param resources Resolves the request's handles.
         * @return The LDR texture id, or 0 when assets are missing.
         */
        uint32_t render(
            Vkm::GL::Context& gl,
            GLView& glView,
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

        /// Free what was released; called once this frame's UI has drawn.
        void freeReleased() { m_released.clear(); }

    private:
        /**
         * @brief Per-key output: an LDR texture in its own FBO, sized per request, and the scene
         *        target of a request that is no square thumbnail.
         */
        struct Entry {
            std::unique_ptr<Vkm::GL::Texture2D> ldr;
            Vkm::GL::FrameBuffer                fbo;
            uint32_t                            width  = 0;
            uint32_t                            height = 0;
            std::unique_ptr<GLTarget>           scene;
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

        std::unique_ptr<Vkm::GL::Shader> m_composite;  ///< Tonemap HDR -> LDR.
        std::unique_ptr<ScreenTriangle>  m_tri;        ///< Fullscreen tonemap draw.

        /**
         * @brief The square thumbnails' shared HDR scene target, at a fixed size.
         *
         * Depth, no G-buffer: a preview runs no screen-space passes.
         */
        GLTarget          m_scratch{GLTarget::Layout::ColorDepth};
        GLCamera          m_camera;    ///< Orbit camera UBO.
        GLOcean           m_ocean;     ///< No sea: a Sea material previews as an ordinary one.
        GLLights          m_lights;    ///< Studio rig lights SSBO.
        GLInstanceBatcher m_batcher;   ///< The request's instanced draws.

        /**
         * @brief The objects a request draws and their transform buffer.
         *
         * The preview's own: it draws between frames, and the frame's buffer holds the scene's.
         */
        RenderObjects  m_object;
        GLObjectBuffer m_objectBuffer;

        std::vector<glm::mat4> m_bones;  ///< The request's palettes end to end, as skinFirst addresses them.
        GLSkinPalette          m_skin;   ///< They, in the buffer the skinned programs read.

        std::vector<uint32_t> m_opaque;       ///< The request's objects drawn first.
        std::vector<uint32_t> m_transparent;  ///< Then those blended over them.

        std::unordered_map<uint64_t, std::unique_ptr<Entry>> m_entries;
        std::vector<std::unique_ptr<Entry>>                   m_released;  ///< Until the UI drew them.
};

} // namespace Vkm::Engine
