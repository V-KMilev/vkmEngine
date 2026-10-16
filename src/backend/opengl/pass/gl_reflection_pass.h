#pragma once

#include <cstdint>

#include "gl_compute_shader.h"
#include "gl_mip_chain_texture.h"
#include "gl_shader.h"

#include "gl_pass.h"
#include "gl_target.h"

namespace Vkm::Engine {

class GLPlanarReflectionPass;

/**
 * @brief Screen-space reflections: the lit scene a glossy pixel's ray meets replaces its probe
 *        or sky reflection.
 *
 * The lit colour goes into a mip chain filtered by shaders/downsample.glsl, so a glossy surface
 * reads the scene blurred to its lobe. A trace marches each glossy pixel's ray through the depth
 * and records the facing surface it met, or none. The resolve swaps the hit colour in through
 * the weight the forward pass wrote: colour + (weight x traced - weight x environment) x trust.
 * Averaging neighbours' rays, weighted by surface likeness, gives a smooth lobe and soft edges
 * without a temporal filter.
 *
 * The forward pass dimmed weight and environment reflection by the fog's transmittance, so the
 * replacement is fogged as the surface is.
 */
class GLReflectionPass : public GLPass {
    public:
        /**
         * @brief Lays @p planar's pictures over the surfaces on its reflectors, where the trace
         *        and the probes cannot see.
         *
         * @param planar The backend's planar reflection pass, run before this one.
         */
        explicit GLReflectionPass(const GLPlanarReflectionPass& planar);
        ~GLReflectionPass() override;

        GLReflectionPass(const GLReflectionPass& other) = delete;
        GLReflectionPass& operator=(const GLReflectionPass& other) = delete;

        GLReflectionPass(GLReflectionPass && other) = delete;
        GLReflectionPass& operator=(GLReflectionPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        /// Enough for a lobe at the roughness limit to span most of the screen.
        static constexpr int MAX_CHAIN_MIPS = 8;

    private:
        /**
         * @brief Fill the colour chain and march each glossy pixel's ray into the hits.
         *
         * @param ctx  The frame.
         * @param step Pixels a hit stands for on each axis.
         */
        void trace(GLFrameContext& ctx, int step);

        /**
         * @brief Tell the resolve where this frame's planar reflectors are, and bind their pictures.
         *
         * @param ctx The frame, whose camera they are placed for.
         */
        void bindPlanar(GLFrameContext& ctx);

        /**
         * @brief (Re)allocate the colour chain and the trace target for a scene size.
         *
         * @param width  The scene's width in pixels.
         * @param height The scene's height in pixels.
         * @param step   Pixels a hit stands for on each axis: 1, or 2 at half resolution.
         */
        void ensureTargets(uint32_t width, uint32_t height, uint32_t step);

    private:
        Vkm::GL::ComputeShader m_source;      ///< Lit colour -> chain base.
        Vkm::GL::ComputeShader m_downsample;  ///< Each chain level from the one before.
        Vkm::GL::Shader        m_trace;       ///< Ray march, a hit a pixel or a 2x2 block.
        Vkm::GL::Shader        m_resolve;     ///< Hits -> the colour chain's next link.

        const GLPlanarReflectionPass& m_planar;

        Vkm::GL::MipChainTexture m_chain;  ///< The lit scene, filtered down.

        /**
         * @brief Per-pixel hit uv, trust, and chain level as a fraction of the last.
         *
         * Unsigned 16-bit: a half float spaces uv past 0.5 almost a pixel
         * apart at 1920 wide, and the resolve reads the colour a hit landed on.
         */
        GLTarget m_hits{GLTarget::Layout::Color, GL_RGBA16};
        uint32_t m_width  = 0;
        uint32_t m_height = 0;
};

} // namespace Vkm::Engine
