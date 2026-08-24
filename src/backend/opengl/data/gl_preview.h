#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "gl_frame_buffer.h"
#include "gl_screen_triangle.h"

#include "gl_target.h"
#include "data/gl_camera.h"
#include "data/gl_lights.h"
#include "data/gl_shadow_data.h"
#include "data/gl_instance_batcher.h"

namespace Vkm::GL {
    class Context;
    class Shader;
    class Texture2D;
}

namespace Vkm::Engine {

class GLView;
class GLIBL;
class GLMesh;
class ResourceManager;
struct PreviewRequest;

/**
 * @brief Renders editor material/mesh previews offscreen.
 *
 * One request = one (mesh, material) pair under a fixed three-light studio
 * rig plus the global IBL, drawn from an orbit camera into a shared HDR
 * scratch target, then tonemapped (the composite shader, bloom off) into a
 * per-key LDR texture the editor displays through ImGui.
 *
 * Follows GLProbeBaker's pattern: it re-binds the camera / lights UBOs with
 * its own per-request data, so it must run outside the main frame's passes
 * (the editor calls it after the scene render; the next frame re-uploads its
 * own UBOs).
 *
 * The rig - three programs and a 512x512 HDR scratch - is built on the first
 * request, not at backend init. Both hosts construct a GLBackend, but only the
 * editor ever asks for a preview, and a shipped game should pay nothing for
 * authoring work it cannot reach.
 */
class GLPreview {
    public:
        GLPreview();
        ~GLPreview();

        GLPreview(const GLPreview& other) = delete;
        GLPreview& operator=(const GLPreview& other) = delete;

        GLPreview(GLPreview && other) = delete;
        GLPreview& operator=(GLPreview && other) = delete;

    public:
        /**
         * @brief Render @p req into its per-key target. Returns the LDR texture id,
         * or 0 when the request can't be drawn (missing assets).
         */
        uint32_t render(Vkm::GL::Context& gl, GLView& glView, const GLIBL& ibl,
                        const PreviewRequest& req, const ResourceManager& resources);

        /**
         * @brief Last-rendered texture for @p key, or 0 when none exists.
         */
        uint32_t texture(uint64_t key) const;

        /**
         * @brief Drop one key's target.
         *
         * Call when the source asset is destroyed so its cached preview is freed.
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
         * @brief Compile the programs and create the scratch target.
         *
         * Called by the first render(), which is the only place a live GL
         * context is guaranteed and the only proof the rig is wanted.
         */
        void init();

    private:
        std::unique_ptr<Vkm::GL::Shader>         m_pbr;       ///< forward PBR (scene draw)
        std::unique_ptr<Vkm::GL::Shader>         m_composite; ///< tonemap HDR -> LDR
        std::unique_ptr<Vkm::GL::Shader>         m_skybox;    ///< sky backdrop (Background::Sky)
        std::unique_ptr<GLMesh>                  m_skyCube;   ///< unit cube for the sky draw
        std::unique_ptr<Vkm::GL::ScreenTriangle> m_tri;       ///< fullscreen tonemap draw

        GLTarget          m_scratch;   ///< shared HDR scene target (fixed size)
        GLCamera          m_camera;    ///< orbit camera UBO (binding 2)
        GLLights          m_lights;    ///< studio rig lights SSBO (binding 0)
        GLShadowData      m_noShadow;  ///< default-built: every light shadowless
        GLInstanceBatcher m_batcher;   ///< single-drawable instanced draw

        std::vector<const DrawableData*> m_drawables;

        std::unordered_map<uint64_t, std::unique_ptr<Entry>> m_entries;
};

} // namespace Vkm::Engine
