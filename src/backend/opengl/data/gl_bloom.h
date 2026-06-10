#pragma once

#include <algorithm>
#include <cstdint>

#include <GL/glew.h>

#include "gl_mip_chain_target.h"

namespace Engine {

/**
 * @brief Mip-chain render target for energy-conserving bloom (COD/Jimenez).
 *
 * An RGBA16F explicit mip chain. The bloom pass progressively downsamples the
 * HDR scene into the chain, then additively upsamples back up; mip 0 holds the
 * final bloom the composite pass blends in. Sized to half the viewport, rebuilt
 * on resize.
 */
class GLBloom : public GLMipChainTarget {
    public:
        static constexpr int MAX_MIPS = 6;

        /// (Re)allocate the chain for a viewport size. Base is half-res.
        void resize(uint32_t viewportWidth, uint32_t viewportHeight) {
            const int w = static_cast<int>(viewportWidth) / 2;
            const int h = static_cast<int>(viewportHeight) / 2;
            if (w <= 0 || h <= 0) return;
            if (m_ready && w == m_baseW && h == m_baseH) return;
            m_baseW = w;
            m_baseH = h;
            createChain();
        }

    private:
        void createChain() {
            releaseAll();

            int minDim = std::min(m_baseW, m_baseH);
            m_mips = 1;
            while ((minDim >> m_mips) >= 2 && m_mips < MAX_MIPS) ++m_mips;

            glGenTextures(1, &m_tex);
            glBindTexture(GL_TEXTURE_2D, m_tex);
            for (int mip = 0; mip < m_mips; ++mip) {
                glTexImage2D(GL_TEXTURE_2D, mip, GL_RGBA16F,
                    mipWidth(mip), mipHeight(mip), 0, GL_RGBA, GL_FLOAT, nullptr);
            }
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, m_mips - 1);
            glBindTexture(GL_TEXTURE_2D, 0);

            glGenFramebuffers(1, &m_fbo);
            m_ready = true;
        }
};

} // namespace Engine
