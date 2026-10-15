#pragma once

#include "gl_compute_shader.h"

#include "gl_pass.h"

namespace Vkm::Engine {

/**
 * @brief Screen-space shadows for the cascaded sun, into the AO target's alpha.
 *
 * Marches the prepass's depth toward the sun for the contacts its cascades are too coarse to
 * catch: Bend Studio's method (shaders/screen_shadow), a few compute dispatches of wavefronts
 * fanned out from the light's place on the screen, read through the AO target's alpha
 * (GLPass::bindAO). Runs after GTAO, whose target it shares; with GTAO off it clears that target
 * first.
 */
class GLScreenShadowPass : public GLPass {
    public:
        GLScreenShadowPass();
        ~GLScreenShadowPass() override;

        GLScreenShadowPass(const GLScreenShadowPass& other) = delete;
        GLScreenShadowPass& operator=(const GLScreenShadowPass& other) = delete;

        GLScreenShadowPass(GLScreenShadowPass && other) = delete;
        GLScreenShadowPass& operator=(GLScreenShadowPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        Vkm::GL::ComputeShader m_shader;
};

} // namespace Vkm::Engine
