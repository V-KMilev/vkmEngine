#pragma once

#include "gl_shader.h"

#include "gl_pass.h"
#include "frame/gl_instance_batcher.h"
#include "frame/gl_occlusion_cull.h"

namespace Vkm::Engine {

/**
 * @brief Lays down opaque depth before the forward pass for early-Z.
 *
 * Primes depth for the opaque/unlit batch so GLForwardPass runs GLDepth::EYE_TEST_EQUAL with depth
 * writes off, writes the G-buffer (the view normal) to color attachment 1, and owns the scene
 * target's clear, so it is unconditional. Alpha-masked geometry is not in this batch. It binds a
 * material's block, for a Sea surface's vertices, and none of its maps.
 *
 * With the GPU occlusion cull on it draws twice, each after a phase of the cull (GLOcclusionCull).
 *
 * Skinned runs sort after static ones, so the two programs switch once. Both must compute
 * gl_Position as the forward programs do, or the primed depth is unusable under
 * GLDepth::EYE_TEST_EQUAL.
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
        /**
         * @brief Bind each of the opaque batch's draws' program and material, and submit it.
         *
         * @param ctx The frame.
         * @param set Which of the cull's lists, while one runs (GLFrameContext::drawOpaque).
         */
        void drawBatch(GLFrameContext& ctx, GLOcclusionCull::Set set);

    private:
        Vkm::GL::Shader m_shader;         ///< Static geometry.
        Vkm::GL::Shader m_skinnedShader;  ///< Skinned, posed by the frame's palette.
};

} // namespace Vkm::Engine
