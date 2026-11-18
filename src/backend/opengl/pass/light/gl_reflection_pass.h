#pragma once

#include <cstdint>

#include "vkm_opengl/shader/compute_shader.h"
#include "vkm_opengl/shader/shader.h"
#include "vkm_opengl/texture/mip_chain_texture.h"
#include "vkm_opengl/texture/texture.h"

#include "pass/gl_pass.h"
#include "storage/gl_target.h"

namespace Vkm::Engine {

/**
 * @brief Screen-space reflections: the lit scene a glossy pixel's ray meets replaces its probe
 *        or sky reflection.
 *
 * A trace marches each glossy pixel's ray through the depth and records the facing surface it
 * met, or none. On a frame where one met something, the lit color goes into a mip chain filtered
 * by shaders/opengl/include/downsample.glsl, so a glossy surface reads the scene blurred to its lobe; on one
 * where none did, the chain's dispatches return at once. The resolve swaps the hit color in through
 * the weight the forward pass wrote: color + (weight x traced - weight x environment) x trust.
 * Averaging neighbors' rays, weighted by surface likeness, gives a smooth lobe and soft edges
 * without a temporal filter.
 *
 * The forward pass dimmed weight and environment reflection by the fog's transmittance, so the
 * replacement is fogged as the surface is, and zeroed them on a material rougher than
 * MAX_ROUGHNESS, which keeps its probe or sky reflection.
 */
class GLReflectionPass : public GLPass {
    public:
        static constexpr int MAX_CHAIN_MIPS = 8;  ///< The most levels a hit reads, the lit scene as level 0
                                                  ///< and the half-size chain's below it: enough for the
                                                  ///< roughest lobe traced, far rippled water's, to span
                                                  ///< most of the screen.

        static constexpr float MAX_ROUGHNESS = 0.25f;  ///< Materials rougher than this reflect the probes and
                                                       ///< the sky alone. The material's own roughness
                                                       ///< decides: what a pixel's distance or ripples add
                                                       ///< blurs its trace, not stops it.
        static constexpr float MAX_DISTANCE  = 50.0f;  ///< World-space trace length.

    public:
        GLReflectionPass();
        ~GLReflectionPass() override;

        GLReflectionPass(const GLReflectionPass& other) = delete;
        GLReflectionPass& operator=(const GLReflectionPass& other) = delete;

        GLReflectionPass(GLReflectionPass && other) = delete;
        GLReflectionPass& operator=(GLReflectionPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        /**
         * @brief March each glossy pixel's ray into the hits, then fill the color chain they read.
         *
         * @param ctx  The frame.
         * @param step Pixels a hit stands for on each axis.
         */
        void trace(GLFrameContext& ctx, int step);

        /**
         * @brief (Re)allocate the color chain and the trace target for a scene size.
         *
         * @param width  The scene's width in pixels.
         * @param height The scene's height in pixels.
         * @param step   Pixels a hit stands for on each axis: 1, or 2 at half resolution.
         */
        void ensureTargets(uint32_t width, uint32_t height, uint32_t step);

    private:
        Vkm::OpenGL::ComputeShader m_downsample;  ///< Each chain level from the one before, or the scene.
        Vkm::OpenGL::Shader        m_trace;       ///< Ray march, a hit a pixel or a 2x2 block.
        Vkm::OpenGL::Shader        m_resolve;     ///< Hits -> the color chain's next link.

        Vkm::OpenGL::MipChainTexture m_chain;  ///< The lit scene, filtered down from half size.
        Vkm::OpenGL::Texture2D       m_found;  ///< One texel: whether this frame's trace met anything.

        GLTarget m_hits{GLTarget::Layout::Color, GL_RGBA16};  ///< Per-pixel hit uv, trust, and chain level as
                                                              ///< a fraction of the last. Unsigned 16-bit: a
                                                              ///< half float spaces uv past 0.5 almost a
                                                              ///< pixel apart at 1920 wide, and the resolve
                                                              ///< reads the color a hit landed on.
};

} // namespace Vkm::Engine
