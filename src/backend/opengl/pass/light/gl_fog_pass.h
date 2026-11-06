#pragma once

#include "vkm_opengl/shader/compute_shader.h"

#include "pass/gl_pass.h"
#include "pass/light/gl_rate_cap.h"

namespace Vkm::Engine {

/**
 * @brief Froxel volumetric fog compute: injection + integration.
 *
 * Runs after the cluster cull (it scatters each froxel's cluster lights) and before anything is
 * lit, since a lit pass fogs what it draws (see GLPass::bindFog). Each light scatters through its
 * own shadow, and the environment's light comes from the sky and the irradiance volume, not the
 * probes, so fog in shade is lit as the walls there are. Injects, then marches each column
 * front-to-back into the integrated volume. A no-op when fog is disabled.
 */
class GLFogPass : public GLPass {
    public:
        GLFogPass();
        ~GLFogPass() override;

        GLFogPass(const GLFogPass& other) = delete;
        GLFogPass& operator=(const GLFogPass& other) = delete;

        GLFogPass(GLFogPass && other) = delete;
        GLFogPass& operator=(GLFogPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        Vkm::OpenGL::ComputeShader m_inject;
        Vkm::OpenGL::ComputeShader m_integrate;
        GLRateCap m_cap{0.5f};      ///< Half a draw out of step with GTAO's, so the two fall on different
                                    ///< frames.
        bool      m_drawn = false;  ///< Whether the volume holds one to read.
};

} // namespace Vkm::Engine
