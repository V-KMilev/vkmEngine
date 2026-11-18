#pragma once

#include <cstdint>

#include "vkm_opengl/shader/compute_shader.h"
#include "vkm_opengl/shader/shader.h"
#include "vkm_opengl/texture/mip_chain_texture.h"

#include "pass/gl_pass.h"
#include "pass/light/gl_rate_cap.h"
#include "storage/gl_target.h"

namespace Vkm::Engine {

/**
 * @brief Ground-Truth Ambient Occlusion (horizon-slice integral).
 *
 * Integrates cosine-weighted visibility over a few screen-space slices of the prepass's depth
 * and G-buffer, giving the AO factor and the bent normal (octahedral, view space). Searched and
 * blurred, edge-aware over 5x5, at half the scene's size, each pixel standing for the scene texel
 * at its 2x2's corner, then widened by depth into the frame's AO target. Runs after the prepass
 * and before the forward draw.
 */
class GLGTAOPass : public GLPass {
    public:
        static constexpr float RADIUS    = 0.6f;  ///< World-space sample radius.
        static constexpr float INTENSITY = 1.0f;  ///< Occlusion strength.
        static constexpr float POWER     = 1.5f;  ///< Contrast curve.

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
         * @brief Size the depth chain to the scene, when it is not already.
         *
         * Full resolution down to a few texels: the search picks a level by a step's reach in
         * pixels, at most a quarter of the screen.
         *
         * @param width  The scene's width in pixels (RenderView::sceneWidth).
         * @param height The scene's height in pixels.
         */
        void ensureDepthMips(uint32_t width, uint32_t height);

    private:
        Vkm::OpenGL::ComputeShader m_prefilter;  ///< The scene depth, linearized and folded into the chain.
        Vkm::OpenGL::Shader        m_shader;
        Vkm::OpenGL::ComputeShader m_denoise;    ///< The raw result, blurred on its own surface.
        Vkm::OpenGL::ComputeShader m_upsample;   ///< The blurred result, widened to the scene's size.
        GLRateCap m_cap{0.0f};      ///< In step with a moved eye; the fog's is half a draw out.
        bool      m_drawn = false;  ///< Whether a frame's target holds one to read.

        Vkm::OpenGL::MipChainTexture m_depthMips;  ///< Linear view depth as a mip chain; nothing outside this
                                                   ///< pass reads it.

        GLTarget m_raw{GLTarget::Layout::Color, GL_RGBA8};      ///< The search's result, half size.
        GLTarget m_blurred{GLTarget::Layout::Color, GL_RGBA8};  ///< The denoise's, half size.
};

} // namespace Vkm::Engine
