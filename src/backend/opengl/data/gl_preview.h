#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "gl_frame_buffer.h"     // Core::FrameBuffer
#include "gl_screen_triangle.h"  // Core::ScreenTriangle

#include "gl_target.h"
#include "data/gl_camera.h"
#include "data/gl_lights.h"
#include "data/gl_shadow_data.h"
#include "data/gl_instance_batcher.h"

namespace Core {
    class Context;
    class Shader;
    class Texture2D;
}

namespace Engine {

class GLView;
class GLIBL;
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
 * own UBOs). Shaders compile in init(), so call that from GLBackend::init.
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
         * @brief Compile the bake programs + create the scratch target. Needs a
         * live GL context (GLBackend::init).
         */
        void init();

        /**
         * @brief Render @p req into its per-key target. Returns the LDR texture id,
         * or 0 when the request can't be drawn (missing assets, no init).
         */
        uint32_t render(Core::Context& gl, GLView& glView, const GLIBL& ibl,
                        const PreviewRequest& req, const ResourceManager& resources);

        /**
         * @brief Last-rendered texture for @p key, or 0 when none exists.
         *
         * @param key  Preview cache key (asset/request identity).
         * @return GL texture id of the cached LDR result, or 0 if nothing is cached.
         */
        uint32_t texture(uint64_t key) const;

        /**
         * @brief Drop one key's target.
         *
         * Call when the source asset is destroyed so its cached preview is freed.
         *
         * @param key Preview cache key whose target is released.
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
            std::unique_ptr<Core::Texture2D> ldr;
            Core::FrameBuffer                fbo;
            uint32_t                         size = 0;
        };

        Entry& ensureEntry(uint64_t key, uint32_t size);

        std::unique_ptr<Core::Shader>         m_pbr;       ///< forward PBR (scene draw)
        std::unique_ptr<Core::Shader>         m_composite; ///< tonemap HDR -> LDR
        std::unique_ptr<Core::ScreenTriangle> m_tri;       ///< fullscreen tonemap draw

        GLTarget          m_scratch;   ///< shared HDR scene target (fixed size)
        GLCamera          m_camera;    ///< orbit camera UBO (binding 2)
        GLLights          m_lights;    ///< studio rig UBO (binding 1)
        GLShadowData      m_noShadow;  ///< default-built: every light shadowless
        GLInstanceBatcher m_batcher;   ///< single-drawable instanced draw

        std::vector<const DrawableData*> m_drawables;

        std::unordered_map<uint64_t, std::unique_ptr<Entry>> m_entries;
};

} // namespace Engine
