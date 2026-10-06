#pragma once

#include "gl_shader.h"

#include "gl_pass.h"
#include "frame/gl_instance_batcher.h"

namespace Vkm::Engine {

/**
 * @brief Lays down opaque depth before the forward pass for early-Z.
 *
 * Primes depth for the opaque/unlit batch so GLForwardPass runs LEQUAL with depth writes off,
 * writes the G-buffer (the view normal) to colour attachment 1, and owns the scene target's
 * clear, so it is unconditional. Alpha-masked geometry is not in this batch. It binds no
 * material: nothing it writes depends on one.
 *
 * Skinned runs sort after static ones, so the two programs switch once. Both must compute
 * gl_Position as the forward programs do, or the primed depth is unusable under LEQUAL.
 */
class GLDepthPrepass : public GLPass {
    public:
        GLDepthPrepass();
        ~GLDepthPrepass() override;

        GLDepthPrepass(const GLDepthPrepass& other) = delete;
        GLDepthPrepass& operator=(const GLDepthPrepass& other) = delete;

        GLDepthPrepass(GLDepthPrepass && other) = delete;
        GLDepthPrepass& operator=(GLDepthPrepass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        Vkm::GL::Shader m_shader;         ///< Static geometry.
        Vkm::GL::Shader m_skinnedShader;  ///< Skinned, posed by the frame's palette.
};

} // namespace Vkm::Engine
