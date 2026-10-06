#pragma once

#include <cstdint>

#include "gl_compute_shader.h"
#include "gl_mip_chain_texture.h"
#include "gl_shader.h"

#include "gl_pass.h"
#include "gl_target.h"

namespace Vkm::Engine {

/**
 * @brief Ground-Truth Ambient Occlusion (horizon-slice integral).
 *
 * Integrates cosine-weighted visibility over a few screen-space slices of the prepass's depth
 * and G-buffer, giving the AO factor and the bent normal (octahedral, view space). An edge-aware
 * 5x5 spatial blur takes the raw result into the frame's AO target. Runs after the prepass and
 * before the forward draw.
 */
class GLGTAOPass : public GLPass {
    public:
        GLGTAOPass();
        ~GLGTAOPass() override;

        GLGTAOPass(const GLGTAOPass& other) = delete;
        GLGTAOPass& operator=(const GLGTAOPass& other) = delete;

        GLGTAOPass(GLGTAOPass && other) = delete;
        GLGTAOPass& operator=(GLGTAOPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        /**
         * @brief Size the depth chain to the viewport, when it is not already.
         *
         * Full resolution down to a few texels: the search picks a level by a step's reach in
         * pixels, at most a quarter of the screen.
         *
         * @param width  Viewport width in pixels.
         * @param height Viewport height in pixels.
         */
        void ensureDepthMips(uint32_t width, uint32_t height);

    private:
        Vkm::GL::ComputeShader m_prefilter;  ///< The scene depth, linearised and folded into the chain.
        Vkm::GL::Shader        m_shader;
        Vkm::GL::ComputeShader m_denoise;    ///< The raw result, blurred on its own surface.

        /**
         * @brief Linear view depth as a mip chain; nothing outside this pass reads it.
         */
        Vkm::GL::MipChainTexture m_depthMips;
        uint32_t m_depthMipsWidth  = 0;
        uint32_t m_depthMipsHeight = 0;

        GLTarget m_raw{GLTarget::Layout::Color, GL_RGBA8};  ///< The search's result, before the denoise.
};

} // namespace Vkm::Engine
