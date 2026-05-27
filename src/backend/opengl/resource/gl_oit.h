#pragma once

#include <memory>
#include <cstdint>

#include <GL/glew.h>

#include "gl_frame_buffer.h"
#include "gl_render_buffer.h"
#include "gl_texture.h"

namespace Engine {

/**
 * @brief Weighted-Blended OIT render target (McGuire-Bavoil 2013).
 *
 * Two color attachments + a single-sample depth renderbuffer:
 *   - color 0: RGBA16F accumulation buffer (rgb * a * w, a * w)
 *   - color 1: R8 revealage (1 - a, with multiplicative blend)
 *   - depth:   single-sample depth renderbuffer, populated from the
 *              opaque MSAA depth via copyDepthFrom() so transparents
 *              still depth-test against the just-rendered opaque scene.
 *
 * Single-sample on purpose - the McGuire-Bavoil formulation does not
 * generalise cleanly to per-sample OIT without gl_SampleID, and the
 * resolved-HDR background we composite over has already lost MSAA.
 *
 * Allocated by GLFrameResources and resized whenever the viewport
 * changes. Pass-side usage: bindForRender() + copyDepthFrom() at the
 * start of the transparent OIT phase; the resolve pass binds the two
 * color textures for reading.
 */
class GLOIT {
    public:
        GLOIT() = default;
        ~GLOIT() = default;

        GLOIT(const GLOIT& other) = delete;
        GLOIT& operator=(const GLOIT& other) = delete;

        GLOIT(GLOIT && other) = delete;
        GLOIT& operator=(GLOIT && other) = delete;

    public:
        void resize(uint32_t width, uint32_t height) {
            if (width == 0 || height == 0) return;
            if (m_ready && width == m_width && height == m_height) return;
            m_width  = width;
            m_height = height;
            createTargets();
        }

        bool isReady() const { return m_ready; }

        uint32_t width()  const { return m_width; }
        uint32_t height() const { return m_height; }
        GLuint   fboId()  const { return m_fbo ? m_fbo->getID() : 0; }

        /**
         * @brief Bind the FBO with both color attachments routed for draw.
         *
         * Sets the viewport and clears accum to (0,0,0,0) and revealage to 1.
         * The depth attachment is preserved across the clear so depth from
         * copyDepthFrom() still applies.
         */
        void bindForRender() const {
            if (!m_ready) return;
            m_fbo->bind();
            const GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
            glDrawBuffers(2, bufs);
            glViewport(0, 0, static_cast<GLsizei>(m_width), static_cast<GLsizei>(m_height));

            // Per-attachment clears - the two outputs need different
            // starting values for the blend math to work.
            const float zero[4]  = { 0.0f, 0.0f, 0.0f, 0.0f };
            const float reveal[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            glClearBufferfv(GL_COLOR, 0, zero);
            glClearBufferfv(GL_COLOR, 1, reveal);
        }

        /**
         * @brief Blit depth from @p srcFboId into this FBO's depth attachment.
         *
         * Source is expected to be MSAA (the opaque HDR FBO); this FBO is
         * single-sample, so the blit performs the depth resolve. Sizes must
         * match.
         */
        void copyDepthFrom(GLuint srcFboId, uint32_t srcW, uint32_t srcH) const {
            if (!m_ready) return;
            if (srcW != m_width || srcH != m_height) return;
            glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFboId);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_fbo->getID());
            glBlitFramebuffer(
                0, 0, static_cast<GLint>(m_width), static_cast<GLint>(m_height),
                0, 0, static_cast<GLint>(m_width), static_cast<GLint>(m_height),
                GL_DEPTH_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        void bindAccumForReading(uint32_t slot) const {
            if (!m_ready) return;
            glActiveTexture(GL_TEXTURE0 + slot);
            glBindTexture(GL_TEXTURE_2D, m_accum->getID());
        }

        void bindRevealageForReading(uint32_t slot) const {
            if (!m_ready) return;
            glActiveTexture(GL_TEXTURE0 + slot);
            glBindTexture(GL_TEXTURE_2D, m_revealage->getID());
        }

    private:
        void createTargets() {
            Core::Texture2DParams pAccum;
            pAccum.width = m_width;
            pAccum.height = m_height;
            pAccum.internalFormat = GL_RGBA16F;
            pAccum.format = GL_RGBA;
            pAccum.type = GL_FLOAT;
            pAccum.wrapS = Core::TextureWrap::ClampToEdge;
            pAccum.wrapT = Core::TextureWrap::ClampToEdge;
            pAccum.minFilter = Core::TextureMinFilter::Linear;
            pAccum.magFilter = Core::TextureMagFilter::Linear;
            pAccum.generateMipmaps = false;
            m_accum = std::make_unique<Core::Texture2D>("oit_accum", pAccum);

            Core::Texture2DParams pReveal = pAccum;
            pReveal.internalFormat = GL_R8;
            pReveal.format = GL_RED;
            pReveal.type = GL_UNSIGNED_BYTE;
            m_revealage = std::make_unique<Core::Texture2D>("oit_revealage", pReveal);

            m_depth = std::make_unique<Core::RenderBuffer>();
            m_depth->bind();
            m_depth->storage(GL_DEPTH_COMPONENT24,
                             static_cast<int32_t>(m_width),
                             static_cast<int32_t>(m_height));
            m_depth->unbind();

            m_fbo = std::make_unique<Core::FrameBuffer>();
            m_fbo->bind();
            m_fbo->attachTexture2D(GL_COLOR_ATTACHMENT0, m_accum->getID());
            m_fbo->attachTexture2D(GL_COLOR_ATTACHMENT1, m_revealage->getID());
            m_fbo->attachRenderBuffer(GL_DEPTH_ATTACHMENT, m_depth->getID());
            m_ready = m_fbo->isComplete();
            m_fbo->unbind();
        }

    private:
        std::unique_ptr<Core::Texture2D>    m_accum;
        std::unique_ptr<Core::Texture2D>    m_revealage;
        std::unique_ptr<Core::RenderBuffer> m_depth;
        std::unique_ptr<Core::FrameBuffer>  m_fbo;

        uint32_t m_width  = 0;
        uint32_t m_height = 0;
        bool     m_ready  = false;
};

} // namespace Engine
