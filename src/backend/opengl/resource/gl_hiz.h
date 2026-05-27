#pragma once

#include <algorithm>
#include <cstdint>

#include <GL/glew.h>

namespace Engine {

/**
 * @brief Hi-Z (max-Z) depth pyramid built from the prepass G-buffer.
 *
 * One R32F texture with an explicit mip chain plus a reusable FBO.
 * Mip 0 stores view-space distance (-pos.z) from the prepass position
 * target; subsequent mips are max(2x2) reductions of the level below.
 * Following the same pattern as GLBloom - raw GL kept encapsulated
 * because the engine's Core::Texture2D doesn't model an explicit
 * per-level render-target chain.
 *
 * No consumer ships in this commit; #20 from RENDERER_PLAN.md will
 * read the pyramid to AABB-test candidate occludees on the CPU side
 * one frame late, or to drive a future GPU-indirect cull pass.
 */
class GLHiZ {
    public:
        GLHiZ() = default;
        ~GLHiZ() { releaseAll(); }

        GLHiZ(const GLHiZ& other) = delete;
        GLHiZ& operator=(const GLHiZ& other) = delete;

        GLHiZ(GLHiZ && other) = delete;
        GLHiZ& operator=(GLHiZ && other) = delete;

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

        bool isReady()  const { return m_ready; }
        int  mipCount() const { return m_mips; }
        GLuint textureId() const { return m_tex; }

        int mipWidth (int mip) const { return std::max(m_baseW >> mip, 1); }
        int mipHeight(int mip) const { return std::max(m_baseH >> mip, 1); }

        /// Bind the pyramid for sampling. The shader picks a level via
        /// textureLod - Nearest filtering keeps the max-reduce bit-exact.
        void bind(uint32_t slot) const {
            glActiveTexture(GL_TEXTURE0 + slot);
            glBindTexture(GL_TEXTURE_2D, m_tex);
        }

        void bindFbo()   const { glBindFramebuffer(GL_FRAMEBUFFER, m_fbo); }
        void unbindFbo() const { glBindFramebuffer(GL_FRAMEBUFFER, 0); }

        /// Point COLOR_ATTACHMENT0 at one chain mip and size the viewport to it.
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
            // Nearest on both axes - the max-reduce must read 4 unfiltered
            // texels exactly; a linear filter would average them and break
            // the conservative max property the occlusion test depends on.
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
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
