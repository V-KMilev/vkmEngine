#pragma once

#include "gl_compute_shader.h"

#include "gl_pass.h"

namespace Vkm::Engine {

/**
 * @brief Froxel volumetric fog compute: injection + integration.
 *
 * Runs after the cluster cull (it scatters each froxel's cluster lights) and before anything is
 * lit, since a lit pass fogs what it draws (see GLPass::bindFog). Injects, then marches each
 * column front-to-back into the integrated volume. A no-op when fog is disabled.
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
        Vkm::GL::ComputeShader m_inject;
        Vkm::GL::ComputeShader m_integrate;
};

} // namespace Vkm::Engine
