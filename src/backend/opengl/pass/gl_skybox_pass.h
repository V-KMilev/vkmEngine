#pragma once

#include <memory>

#include "gl_pass.h"

namespace Vkm::GL {
    class Shader;
}

namespace Vkm::Engine {

class GLMesh;

/**
 * @brief Draws the baked environment cubemap as the scene background.
 *
 * Runs after the depth prepass and before the forward draw, into the HDR target,
 * at the far plane (depth func LEQUAL, no depth write), so it fills only
 * background pixels (depth == far from the prepass) without disturbing the
 * geometry the forward pass then draws over it. Outputs linear radiance - the
 * composite pass tone-maps it together with lit geometry. No-ops until the IBL
 * bake is ready.
 */
class GLSkyboxPass : public GLPass {
    public:
        GLSkyboxPass();
        ~GLSkyboxPass() override;

        GLSkyboxPass(const GLSkyboxPass& other) = delete;
        GLSkyboxPass& operator=(const GLSkyboxPass& other) = delete;

        GLSkyboxPass(GLSkyboxPass && other) = delete;
        GLSkyboxPass& operator=(GLSkyboxPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        std::unique_ptr<Vkm::GL::Shader> m_shader;
        std::unique_ptr<GLMesh>        m_cube;
};

} // namespace Vkm::Engine
