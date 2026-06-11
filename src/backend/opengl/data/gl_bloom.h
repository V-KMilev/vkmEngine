#pragma once

#include <algorithm>
#include <cstdint>

#include <GL/glew.h>

#include "gl_mip_chain_texture.h"  // Core::MipChainTexture

namespace Engine {

/**
 * @brief Mip-chain render target for energy-conserving bloom (COD/Jimenez).
 *
 * An RGBA16F explicit mip chain (Core::MipChainTexture). The bloom pass
 * progressively downsamples the HDR scene into the chain, then additively
 * upsamples back up; mip 0 holds the final bloom the composite pass blends in.
 * Sized to half the viewport, rebuilt on resize. This class owns only the
 * bloom-specific policy (half-res, mip count, format); the chain owns the GL.
 */
class GLBloom {
    public:
        GLBloom() = default;
        ~GLBloom() = default;

        GLBloom(const GLBloom& other) = delete;
        GLBloom& operator=(const GLBloom& other) = delete;

        GLBloom(GLBloom && other) = delete;
        GLBloom& operator=(GLBloom && other) = delete;

    public:
        static constexpr int MAX_MIPS = 6;

        /// (Re)allocate the chain for a viewport size. Base is half-res.
        void resize(uint32_t viewportWidth, uint32_t viewportHeight) {
            const int w = static_cast<int>(viewportWidth) / 2;
            const int h = static_cast<int>(viewportHeight) / 2;
            if (w <= 0 || h <= 0) return;
            if (m_chain.isReady() && w == m_baseW && h == m_baseH) return;
            m_baseW = w;
            m_baseH = h;

            int minDim = std::min(w, h);
            int mips = 1;
            while ((minDim >> mips) >= 2 && mips < MAX_MIPS) ++mips;

            m_chain.create(w, h, mips, GL_RGBA16F, GL_RGBA, GL_FLOAT,
                GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR);
        }

        bool isReady()  const { return m_chain.isReady(); }
        int  mipCount() const { return m_chain.mipCount(); }

        /// Bind the chain for sampling; the shader selects a level via textureLod.
        void bind(uint32_t slot) const { m_chain.bindSlot(slot); }

        /// Per-mip render-target ops for the downsample / upsample loop.
        void bindFbo()   const { m_chain.bindFbo(); }
        void unbindFbo() const { m_chain.unbindFbo(); }
        void attachMip(int mip) const { m_chain.attachMip(mip); }

    private:
        Core::MipChainTexture m_chain;
        int m_baseW = 0;
        int m_baseH = 0;
};

} // namespace Engine
