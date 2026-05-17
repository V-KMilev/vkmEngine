#pragma once

#include <memory>
#include <cstdint>

#include <GL/glew.h>

#include "gl_frame_buffer.h"
#include "gl_texture.h"

namespace Engine {

/**
 * @brief Shared full-res RGBA16F scratch target for in-place post passes.
 *
 * Passes that must transform the resolved HDR scene (DoF, motion blur) can't
 * sample and write the same texture, so they render here, then blit back into
 * the HDR resolve target for the downstream chain. One texture + FBO,
 * viewport-sized, rebuilt on resize.
 */
class GLPostScratch {
    public:
        GLPostScratch() = default;
        ~GLPostScratch() = default;

        GLPostScratch(const GLPostScratch& other) = delete;
        GLPostScratch& operator=(const GLPostScratch& other) = delete;

        GLPostScratch(GLPostScratch && other) = delete;
        GLPostScratch& operator=(GLPostScratch && other) = delete;

    public:
        void resize(uint32_t width, uint32_t height) {
            if (width == 0 || height == 0) return;
            if (m_ready && width == m_width && height == m_height) return;
            m_width  = width;
            m_height = height;
            createTargets();
        }

        bool isReady() const { return m_ready; }

        /// Bind the scratch FBO and set the viewport for a fullscreen draw.
        void bindForRender() const {
            if (!m_ready) return;
            m_fbo->bind();
            glViewport(0, 0, static_cast<GLsizei>(m_width), static_cast<GLsizei>(m_height));
        }

        GLuint fboId()   const { return m_fbo ? m_fbo->getID() : 0; }
        uint32_t width()  const { return m_width; }
        uint32_t height() const { return m_height; }

    private:
        void createTargets() {
            Core::Texture2DParams p;
            p.width = m_width;
            p.height = m_height;
            p.internalFormat = GL_RGBA16F;
            p.format = GL_RGBA;
            p.type = GL_FLOAT;
            p.wrapS = Core::TextureWrap::ClampToEdge;
            p.wrapT = Core::TextureWrap::ClampToEdge;
            p.minFilter = Core::TextureMinFilter::Linear;
            p.magFilter = Core::TextureMagFilter::Linear;
            p.generateMipmaps = false;
            m_color = std::make_unique<Core::Texture2D>("post_scratch", p);

            m_fbo = std::make_unique<Core::FrameBuffer>();
            m_fbo->bind();
            m_fbo->attachTexture2D(GL_COLOR_ATTACHMENT0, m_color->getID());
            m_ready = m_fbo->isComplete();
            m_fbo->unbind();
        }

    private:
        std::unique_ptr<Core::Texture2D>   m_color;
        std::unique_ptr<Core::FrameBuffer> m_fbo;

        uint32_t m_width  = 0;
        uint32_t m_height = 0;
        bool     m_ready  = false;
};

} // namespace Engine
