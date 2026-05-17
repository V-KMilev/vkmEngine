#pragma once

#include <cstdint>

#include <GL/glew.h>

namespace Engine {

/**
 * @brief GPU targets for histogram-free auto-exposure (eye adaptation).
 *
 * A square R16F luminance texture with a full mip chain (its top 1x1 mip is
 * the geometric-mean scene luminance, produced with glGenerateMipmap), plus a
 * ping-pong pair of 1x1 R16F textures holding the temporally adapted value.
 * Fixed-size and viewport-independent. Raw GL - tiny single-channel render
 * targets are outside the current Core wrappers.
 */
class GLAutoExposure {
    public:
        GLAutoExposure() = default;
        ~GLAutoExposure() { releaseAll(); }

        GLAutoExposure(const GLAutoExposure& other) = delete;
        GLAutoExposure& operator=(const GLAutoExposure& other) = delete;

        GLAutoExposure(GLAutoExposure && other) = delete;
        GLAutoExposure& operator=(GLAutoExposure && other) = delete;

    public:
        static constexpr int LUM_SIZE = 256;  ///< Log-luminance reduction target
        static constexpr int LUM_MIPS = 9;    ///< log2(256) + 1, top mip is 1x1

        void createTargets() {
            if (m_lumTex) return;

            glGenTextures(1, &m_lumTex);
            glBindTexture(GL_TEXTURE_2D, m_lumTex);
            for (int mip = 0; mip < LUM_MIPS; ++mip) {
                const int s = LUM_SIZE >> mip;
                glTexImage2D(GL_TEXTURE_2D, mip, GL_R16F, s, s, 0, GL_RED, GL_FLOAT, nullptr);
            }
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, LUM_MIPS - 1);

            // Seed adaptation to mid-grey so the first frames are not blown out.
            const float seed = 0.18f;
            for (int i = 0; i < 2; ++i) {
                glGenTextures(1, &m_adapt[i]);
                glBindTexture(GL_TEXTURE_2D, m_adapt[i]);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, 1, 1, 0, GL_RED, GL_FLOAT, &seed);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            }
            glBindTexture(GL_TEXTURE_2D, 0);

            glGenFramebuffers(1, &m_fbo);
            m_ready = true;
        }

        bool isReady() const { return m_ready; }

        /// Make the just-written adapted value the current one.
        void swap() { m_cur = 1 - m_cur; }

        /// Bind / unbind the metering framebuffer.
        void bindFbo()   const { glBindFramebuffer(GL_FRAMEBUFFER, m_fbo); }
        void unbindFbo() const { glBindFramebuffer(GL_FRAMEBUFFER, 0); }

        /// Step 1: target the log-luminance texture (mip 0), full LUM_SIZE.
        void attachLum() const {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D, m_lumTex, 0);
            glViewport(0, 0, LUM_SIZE, LUM_SIZE);
        }

        /// Reduce the log-luminance image down its mip chain; the 1x1 top
        /// mip is the geometric-mean scene luminance.
        void reduceLum() const {
            glBindTexture(GL_TEXTURE_2D, m_lumTex);
            glGenerateMipmap(GL_TEXTURE_2D);
        }

        /// Bind the log-luminance pyramid as a sampler input.
        void bindLum(uint32_t slot) const {
            glActiveTexture(GL_TEXTURE0 + slot);
            glBindTexture(GL_TEXTURE_2D, m_lumTex);
        }

        /// Bind the previous adapted value (also the latest after swap());
        /// the composite reads this to derive exposure.
        void bindAdapted(uint32_t slot) const {
            glActiveTexture(GL_TEXTURE0 + slot);
            glBindTexture(GL_TEXTURE_2D, m_adapt[m_cur]);
        }

        /// Step 2: target the adapted value this frame writes into (1x1).
        void attachAdaptWrite() const {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D, m_adapt[1 - m_cur], 0);
            glViewport(0, 0, 1, 1);
        }

    private:
        void releaseAll() noexcept {
            if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
            if (m_lumTex) glDeleteTextures(1, &m_lumTex);
            if (m_adapt[0]) glDeleteTextures(1, &m_adapt[0]);
            if (m_adapt[1]) glDeleteTextures(1, &m_adapt[1]);
            m_fbo = m_lumTex = m_adapt[0] = m_adapt[1] = 0;
        }

    private:
        GLuint m_lumTex   = 0;
        GLuint m_adapt[2] = {0, 0};
        GLuint m_fbo      = 0;
        int    m_cur      = 0;
        bool   m_ready    = false;
};

} // namespace Engine
