#pragma once

#include <GL/glew.h>

#include "render/render_target.h"
#include "gl_frame_buffer.h"
#include "gl_render_buffer.h"

namespace Engine {

/**
 * @brief Render target representing the default framebuffer (screen).
 *
 * Binds framebuffer 0 and sets the viewport to the stored dimensions.
 */
class GLDefaultRenderTarget : public RenderTarget {
    public:
        GLDefaultRenderTarget() = default;

        void bind() override {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, m_width, m_height);
        }

        void unbind() override {
            // Already the default — nothing to do
        }

        void resize(uint32_t width, uint32_t height) override {
            m_width = width;
            m_height = height;
        }

        uint32_t getWidth()  const override { return m_width; }
        uint32_t getHeight() const override { return m_height; }

    private:
        uint32_t m_width  = 0;
        uint32_t m_height = 0;
};

/**
 * @brief Render target backed by an offscreen OpenGL FBO.
 *
 * Wraps a Core::FrameBuffer with a color texture attachment and a depth
 * renderbuffer. Suitable for shadow maps, post-processing, or any
 * offscreen rendering.
 *
 * Usage:
 *   GLFramebufferTarget target(1024, 768);
 *   target.bind();     // render into FBO
 *   target.unbind();   // restore default framebuffer
 *   // Use getColorTexture() to sample the result
 */
class GLFramebufferTarget : public RenderTarget {
    public:
        GLFramebufferTarget(uint32_t width, uint32_t height)
            : m_width(width), m_height(height)
        {
            createAttachments();
        }

        ~GLFramebufferTarget() override {
            destroyAttachments();
        }

        GLFramebufferTarget(const GLFramebufferTarget&) = delete;
        GLFramebufferTarget& operator=(const GLFramebufferTarget&) = delete;
        GLFramebufferTarget(GLFramebufferTarget&&) = delete;
        GLFramebufferTarget& operator=(GLFramebufferTarget&&) = delete;

        void bind() override {
            m_fbo.bind();
            glViewport(0, 0, m_width, m_height);
        }

        void unbind() override {
            m_fbo.unbind();
        }

        void resize(uint32_t width, uint32_t height) override {
            if (width == m_width && height == m_height) return;
            m_width = width;
            m_height = height;
            destroyAttachments();
            createAttachments();
        }

        uint32_t getWidth()  const override { return m_width; }
        uint32_t getHeight() const override { return m_height; }

        /** @brief Get the color attachment texture ID for sampling. */
        GLuint getColorTexture() const { return m_colorTexture; }

    private:
        void createAttachments() {
            // Color texture (RGBA8)
            glGenTextures(1, &m_colorTexture);
            glBindTexture(GL_TEXTURE_2D, m_colorTexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);

            // Depth renderbuffer
            glGenRenderbuffers(1, &m_depthRBO);
            glBindRenderbuffer(GL_RENDERBUFFER, m_depthRBO);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_width, m_height);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);

            // Attach to FBO
            m_fbo.bind();
            m_fbo.attachTexture2D(GL_COLOR_ATTACHMENT0, m_colorTexture);
            m_fbo.attachRenderBuffer(GL_DEPTH_STENCIL_ATTACHMENT, m_depthRBO);
            m_fbo.unbind();
        }

        void destroyAttachments() {
            if (m_colorTexture) {
                glDeleteTextures(1, &m_colorTexture);
                m_colorTexture = 0;
            }
            if (m_depthRBO) {
                glDeleteRenderbuffers(1, &m_depthRBO);
                m_depthRBO = 0;
            }
        }

    private:
        Core::FrameBuffer m_fbo;
        GLuint m_colorTexture = 0;
        GLuint m_depthRBO     = 0;
        uint32_t m_width;
        uint32_t m_height;
};

} // namespace Engine
