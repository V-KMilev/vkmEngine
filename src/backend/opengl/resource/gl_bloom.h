#pragma once

#include <algorithm>
#include <cstdint>

#include <GL/glew.h>

namespace Engine {

/**
 * @brief Mip-chain render target for energy-conserving bloom (COD/Jimenez).
 *
 * One RGBA16F texture with an explicit mip chain plus a reusable FBO. The
 * bloom pass progressively downsamples the resolved HDR scene into the
 * chain, then additively upsamples back up; mip 0 holds the final bloom the
 * composite pass blends in. A per-level render-target mip chain is outside
 * what Core::Texture2D models, so the raw GL is deliberately kept
 * encapsulated here behind intention-revealing ops (bind / bindFbo /
 * attachMip) - passes never touch GL directly. Sized to half the viewport,
 * rebuilt on resize.
 */
class GLBloom {
    public:
        GLBloom() = default;
        ~GLBloom() { releaseAll(); }

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
            if (m_ready && w == m_baseW && h == m_baseH) return;
            m_baseW = w;
            m_baseH = h;
            createChain();
        }

        bool isReady()  const { return m_ready; }
        int  mipCount() const { return m_mips; }

        int mipWidth(int mip)  const { return std::max(m_baseW >> mip, 1); }
        int mipHeight(int mip) const { return std::max(m_baseH >> mip, 1); }

        /// Bind the bloom mip chain as a sampler input. The shader selects a
        /// level via its u_srcLod uniform; mip 0 is the final bloom that the
        /// composite samples (binds 0 when not ready, a harmless no-op since
        /// the composite zeroes bloom strength in that case).
        void bind(uint32_t slot) const {
            glActiveTexture(GL_TEXTURE0 + slot);
            glBindTexture(GL_TEXTURE_2D, m_tex);
        }

        /// Bind / unbind the chain's framebuffer for the down/upsample loop.
        void bindFbo()   const { glBindFramebuffer(GL_FRAMEBUFFER, m_fbo); }
        void unbindFbo() const { glBindFramebuffer(GL_FRAMEBUFFER, 0); }

        /// Point COLOR_ATTACHMENT0 at one chain mip and size the viewport to
        /// it - the render target for that down/upsample step.
        void attachMip(int mip) const {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D, m_tex, mip);
            glViewport(0, 0, mipWidth(mip), mipHeight(mip));
        }

    private:
        void releaseAll() noexcept {
            if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
            if (m_tex) glDeleteTextures(1, &m_tex);
            m_fbo = 0;
            m_tex = 0;
        }

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

    private:
        GLuint m_tex = 0;
        GLuint m_fbo = 0;
        int    m_baseW = 0;
        int    m_baseH = 0;
        int    m_mips  = 1;
        bool   m_ready = false;
};

} // namespace Engine
