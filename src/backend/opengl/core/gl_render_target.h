#pragma once

#include <memory>

#include <GL/glew.h>

#include "system/render/render_target.h"
#include "gl_frame_buffer.h"
#include "gl_render_buffer.h"
#include "gl_texture.h"

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

        ~GLFramebufferTarget() override = default;

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
            createAttachments();
        }

        uint32_t getWidth()  const override { return m_width; }
        uint32_t getHeight() const override { return m_height; }

        /** @brief Get the color attachment texture ID for sampling. */
        GLuint getColorTexture() const { return m_colorTexture ? m_colorTexture->getID() : 0; }

    private:
        void createAttachments() {
            Core::Texture2DParams texParams;
            texParams.width = m_width;
            texParams.height = m_height;
            texParams.internalFormat = GL_RGBA8;
            texParams.format = GL_RGBA;
            texParams.type = GL_UNSIGNED_BYTE;
            texParams.wrapS = Core::TextureWrap::ClampToEdge;
            texParams.wrapT = Core::TextureWrap::ClampToEdge;
            texParams.minFilter = Core::TextureMinFilter::Linear;
            texParams.magFilter = Core::TextureMagFilter::Linear;
            texParams.generateMipmaps = false;
            m_colorTexture = std::make_unique<Core::Texture2D>("framebuffer_color", texParams);

            m_depthRBO = std::make_unique<Core::RenderBuffer>();
            m_depthRBO->storage(GL_DEPTH24_STENCIL8, m_width, m_height);

            m_fbo.bind();
            m_fbo.attachTexture2D(GL_COLOR_ATTACHMENT0, m_colorTexture->getID());
            m_fbo.attachRenderBuffer(GL_DEPTH_STENCIL_ATTACHMENT, m_depthRBO->getID());
            m_fbo.unbind();
        }

    private:
        Core::FrameBuffer m_fbo;
        std::unique_ptr<Core::Texture2D> m_colorTexture;
        std::unique_ptr<Core::RenderBuffer> m_depthRBO;
        uint32_t m_width;
        uint32_t m_height;
};

} // namespace Engine
