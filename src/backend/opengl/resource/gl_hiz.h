#pragma once

#include <algorithm>
#include <cstdint>

#include <GL/glew.h>

#include "gl_mip_chain_target.h"

namespace Engine {

/**
 * @brief Hi-Z (max-Z) depth pyramid built from the prepass G-buffer.
 *
 * An R32F explicit mip chain (lifecycle in GLMipChainTarget). Mip 0 stores
 * view-space distance (-pos.z) from the prepass position target; subsequent
 * mips are max(2x2) reductions of the level below. NEAREST filtering keeps the
 * max-reduce bit-exact - a linear filter would average the 4 texels and break
 * the conservative max property the occlusion test depends on.
 *
 * The visibility system reads back one mip via OcclusionOracle to AABB-test
 * candidate occludees on the CPU side one frame late; a future GPU-indirect
 * cull pass would consume the full pyramid on the GPU.
 */
class GLHiZ : public GLMipChainTarget {
    public:
        static constexpr int MAX_MIPS = 12;  // 4096+ viewport upper bound

        void resize(uint32_t viewportWidth, uint32_t viewportHeight) {
            const int w = static_cast<int>(viewportWidth);
            const int h = static_cast<int>(viewportHeight);
            if (w <= 0 || h <= 0) return;
            if (m_ready && w == m_baseW && h == m_baseH) return;
            m_baseW = w;
            m_baseH = h;
            createChain();
        }

    private:
        void createChain() {
            releaseAll();

            int maxDim = std::max(m_baseW, m_baseH);
            m_mips = 1;
            while ((maxDim >> m_mips) >= 1 && m_mips < MAX_MIPS) ++m_mips;

            glGenTextures(1, &m_tex);
            glBindTexture(GL_TEXTURE_2D, m_tex);
            for (int mip = 0; mip < m_mips; ++mip) {
                glTexImage2D(GL_TEXTURE_2D, mip, GL_R32F,
                    mipWidth(mip), mipHeight(mip), 0, GL_RED, GL_FLOAT, nullptr);
            }
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, m_mips - 1);
            glBindTexture(GL_TEXTURE_2D, 0);

            glGenFramebuffers(1, &m_fbo);
            m_ready = true;
        }
};

} // namespace Engine
