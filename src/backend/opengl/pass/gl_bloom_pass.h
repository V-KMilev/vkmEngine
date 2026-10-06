#pragma once

#include "gl_compute_shader.h"

#include "gl_pass.h"

namespace Vkm::Engine {

/**
 * @brief Thresholded bloom over the HDR scene (COD/Jimenez).
 *
 * Downsamples the complete HDR scene into the mip chain (Karis-averaged, soft-knee first tap),
 * then additively upsamples with a 3x3 tent; mip 0 ends holding the light past the threshold
 * (see GLFrameContext::bloomReady). Each level is a compute dispatch: on this backend's driver
 * a framebuffer bind and a draw cost the CPU several times a dispatch.
 */
class GLBloomPass : public GLPass {
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
        Vkm::GL::ComputeShader m_down;
        Vkm::GL::ComputeShader m_up;
};

} // namespace Vkm::Engine
