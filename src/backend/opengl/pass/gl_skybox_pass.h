#pragma once

#include "gl_shader.h"

#include "gl_pass.h"

namespace Vkm::Engine {

/**
 * @brief Draws the baked environment cubemap as the scene background.
 *
 * Runs after the depth prepass and the fog compute, before the forward draw, at the far plane
 * (LEQUAL, no depth write), so it fills only background pixels; outputs linear radiance, fogged.
 * With no sky to show it still draws while there is fog, which lies in front of the black
 * background too; with neither it is a no-op.
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
        Vkm::GL::Shader m_shader;
};

} // namespace Vkm::Engine
