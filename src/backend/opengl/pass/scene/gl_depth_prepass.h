#pragma once

#include "vkm_opengl/shader/shader.h"

#include "pass/gl_pass.h"
#include "frame/draw/gl_instance_batcher.h"
#include "frame/draw/gl_occlusion_cull.h"

namespace Vkm::Engine {

/**
 * @brief Lays down opaque depth before the forward pass for early-Z.
 *
 * Primes depth for the opaque/unlit batch so GLForwardPass runs GLDepth::EYE_TEST_EQUAL with depth
 * writes off, writes the G-buffer (the view normal) to color attachment 1, and owns the scene
 * target's clear, so it is unconditional. Alpha-masked geometry is not in this batch. It binds no
 * material: water's share of the waves is its body's (SSBO_WATER_BODIES).
 *
 * With the GPU occlusion cull on it draws twice, each after a phase of the cull (GLOcclusionCull).
 *
 * Water runs sort after the other static ones, and skinned runs after both, so the three programs
 * switch twice at most. Each must compute gl_Position as its forward program does, or the primed
 * depth is unusable under GLDepth::EYE_TEST_EQUAL.
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
         * @brief Bind each of the opaque batch's draws' program, and submit it.
         *
         * @param ctx The frame.
         * @param set Which of the cull's lists, while one runs (GLFrameContext::drawOpaque).
         */
        void drawBatch(GLFrameContext& ctx, GLOcclusionCull::Set set);

    private:
        Vkm::OpenGL::Shader m_shader;         ///< Static geometry.
        Vkm::OpenGL::Shader m_skinnedShader;  ///< Skinned, posed by the frame's palette.
        Vkm::OpenGL::Shader m_waterShader;    ///< Water, which the sea's waves move.
};

} // namespace Vkm::Engine
