#pragma once

#include <memory>
#include <vector>

#include "gl_context.h"  // Core::Context (held by value)

#include "system/render/render_backend.h"
#include "gl_view.h"
#include "gl_target.h"
#include "gl_ao_target.h"
#include "data/gl_camera.h"
#include "data/gl_lights.h"
#include "data/gl_shadow_atlas.h"
#include "data/gl_shadow_data.h"
#include "data/gl_ibl.h"
#include "data/gl_bloom.h"

namespace Core {
    class UniformBuffer;
}

namespace Engine {

class GLPass;
class GLProbeArray;
class GLProbeBaker;

/**
 * @brief The OpenGL implementation of RenderBackend.
 *
 * Owns the GL context state, the GPU resource mirror (GLView), and an ordered
 * list of passes. render() syncs the frame's resources, clears, then runs each
 * pass in order. The window's buffer swap stays in the engine loop (it must
 * happen after the editor UI draws), so this backend draws but does not present.
 */
class GLBackend : public RenderBackend {
    public:
        GLBackend();
        ~GLBackend() override;

        GLBackend(const GLBackend& other) = delete;
        GLBackend& operator=(const GLBackend& other) = delete;

        GLBackend(GLBackend && other) = delete;
        GLBackend& operator=(GLBackend && other) = delete;

    public:
        bool init(WindowManager& window) override;
        void resize(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
        void render(const RenderView& view, const ResourceManager& resources) override;

    private:
        Core::Context m_context;
        GLView        m_view;
        GLTarget      m_sceneHDR;
        GLTarget      m_sceneColor;
        GLAOTarget    m_ao;

        GLCamera      m_camera;
        GLLights      m_lights;

        GLShadowAtlas m_shadowAtlas;
        GLShadowData  m_shadowData;

        GLIBL         m_ibl;
        GLBloom       m_bloom;

        std::unique_ptr<GLProbeBaker>        m_probeBaker;  ///< Created in init(); bakes probes at frame end.
        std::unique_ptr<GLProbeArray>        m_probeArray;  ///< Shared cube-map arrays (created in init()).
        std::vector<bool>                    m_probeBaked;  ///< Per scene-probe (= array layer) bake state.
        std::unique_ptr<Core::UniformBuffer> m_probeUBO;    ///< ProbeBlock: boxes + layers (binding 4).

        std::vector<std::unique_ptr<GLPass>> m_passes;
};

} // namespace Engine
