#pragma once

#include "vkm_opengl/shader/compute_shader.h"

#include "pass/gl_pass.h"

namespace Vkm::Engine {

/**
 * @brief Thresholded bloom over the HDR scene (Jimenez 2014).
 *
 * Downsamples the complete HDR scene into the mip chain (Karis-averaged, soft-knee first tap),
 * then additively upsamples with a 3x3 tent; mip 0 ends holding the light past the threshold
 * (see GLFrameContext::bloomReady). Each level is a compute dispatch: on this backend's driver
 * a framebuffer bind and a draw cost the CPU several times a dispatch.
 */
class GLBloomPass : public GLPass {
    public:
        static constexpr float STRENGTH  = 0.06f;   ///< Bloom scale, added pre-tonemap.
        static constexpr float THRESHOLD = 2.0f;    ///< On a pixel's brightest channel, after the exposure:
                                                    ///< past a sunlit white's, so what glows is a highlight,
                                                    ///< an emitter or the sun.
        static constexpr float KNEE      = 0.5f;    ///< Soft-knee width around the threshold.
        static constexpr float RADIUS    = 0.005f;  ///< Upsample tent radius, fraction of frame width.

    public:
        GLBloomPass();
        ~GLBloomPass() override;

        GLBloomPass(const GLBloomPass& other) = delete;
        GLBloomPass& operator=(const GLBloomPass& other) = delete;

        GLBloomPass(GLBloomPass && other) = delete;
        GLBloomPass& operator=(GLBloomPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        Vkm::OpenGL::ComputeShader m_down;
        Vkm::OpenGL::ComputeShader m_up;
};

} // namespace Vkm::Engine
