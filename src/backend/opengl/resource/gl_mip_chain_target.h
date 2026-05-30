#pragma once

#include <algorithm>
#include <cstdint>

#include <GL/glew.h>

namespace Engine {

/**
 * @brief Shared RAII for a single-texture explicit mip-chain render target.
 *
 * One GL texture with an explicit per-level mip chain plus a reusable FBO - the
 * shape GLBloom and GLHiZ both need (Core::Texture2D doesn't model a per-level
 * render-target chain, so the raw GL is kept encapsulated here). Subclasses own
 * the format / filter / sizing policy via their own resize() + createChain();
 * this base owns the handles, their release, and the bind / attach / query ops
 * that don't vary between the two.
 *
 * Not used polymorphically (callers hold concrete GLBloom / GLHiZ), so the
 * dtor is intentionally non-virtual - it still runs on concrete destruction.
 */
class GLMipChainTarget {
    public:
        GLMipChainTarget() = default;
        ~GLMipChainTarget() { releaseAll(); }

        GLMipChainTarget(const GLMipChainTarget& other) = delete;
        GLMipChainTarget& operator=(const GLMipChainTarget& other) = delete;

        GLMipChainTarget(GLMipChainTarget && other) = delete;
        GLMipChainTarget& operator=(GLMipChainTarget && other) = delete;

    public:
        bool   isReady()   const { return m_ready; }
        int    mipCount()  const { return m_mips; }
        GLuint textureId() const { return m_tex; }

        int mipWidth (int mip) const { return std::max(m_baseW >> mip, 1); }
        int mipHeight(int mip) const { return std::max(m_baseH >> mip, 1); }

        /// Bind the chain texture for sampling. The shader selects a level via
        /// its LOD uniform / textureLod.
        void bind(uint32_t slot) const {
            glActiveTexture(GL_TEXTURE0 + slot);
            glBindTexture(GL_TEXTURE_2D, m_tex);
        }

        /// Bind / unbind the chain's framebuffer for the per-mip loop.
        void bindFbo()   const { glBindFramebuffer(GL_FRAMEBUFFER, m_fbo); }
        void unbindFbo() const { glBindFramebuffer(GL_FRAMEBUFFER, 0); }

        /// Point COLOR_ATTACHMENT0 at one chain mip and size the viewport to it.
        void attachMip(int mip) const {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D, m_tex, mip);
            glViewport(0, 0, mipWidth(mip), mipHeight(mip));
        }

    protected:
        void releaseAll() noexcept {
            if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
            if (m_tex) glDeleteTextures(1, &m_tex);
            m_fbo = 0;
            m_tex = 0;
        }

        GLuint m_tex   = 0;
        GLuint m_fbo   = 0;
        int    m_baseW = 0;
        int    m_baseH = 0;
        int    m_mips  = 1;
        bool   m_ready = false;
};

} // namespace Engine
