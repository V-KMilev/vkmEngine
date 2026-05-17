#pragma once

#include <memory>
#include <cstdint>

#include <GL/glew.h>

#include "gl_frame_buffer.h"
#include "gl_texture.h"

namespace Engine {

/**
 * @brief Ping-pong history target for temporal anti-aliasing.
 *
 * Two single-sample RGBA16F colour buffers: the pass reads last frame's
 * accumulation and writes this frame's into the other, then swaps. One FBO,
 * re-pointed per frame. Viewport-sized; rebuilt on resize.
 */
class GLTAA {
    public:
        GLTAA() = default;
        ~GLTAA() = default;

        GLTAA(const GLTAA& other) = delete;
        GLTAA& operator=(const GLTAA& other) = delete;

        GLTAA(GLTAA && other) = delete;
        GLTAA& operator=(GLTAA && other) = delete;

    public:
        void resize(uint32_t width, uint32_t height) {
            if (width == 0 || height == 0) return;
            if (m_ready && width == m_width && height == m_height) return;
            m_width  = width;
            m_height = height;
            createTargets();
            m_primed = false;  // history is stale after a resize
        }

        bool isReady() const { return m_ready; }

        /// First frame after (re)creation has no valid history yet.
        bool primed() const { return m_primed; }
        void markPrimed()   { m_primed = true; }

        /// Bind last frame's accumulated history to a sampler slot.
        void bindHistory(uint32_t slot) const {
            if (m_hist[m_cur]) m_hist[m_cur]->bindSlot(slot);
        }

        /// Bind the FBO with the write texture attached, set the viewport.
        void bindWrite() {
            if (!m_ready) return;
            m_fbo->bind();
            m_fbo->attachTexture2D(GL_COLOR_ATTACHMENT0, m_hist[1 - m_cur]->getID());
            glViewport(0, 0, static_cast<GLsizei>(m_width), static_cast<GLsizei>(m_height));
        }

        GLuint fboId() const { return m_fbo ? m_fbo->getID() : 0; }

        void swap() { m_cur = 1 - m_cur; }

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

            m_hist[0] = std::make_unique<Core::Texture2D>("taa_history_0", p);
            m_hist[1] = std::make_unique<Core::Texture2D>("taa_history_1", p);

            m_fbo = std::make_unique<Core::FrameBuffer>();
            m_fbo->bind();
            m_fbo->attachTexture2D(GL_COLOR_ATTACHMENT0, m_hist[0]->getID());
            m_ready = m_fbo->isComplete();
            m_fbo->unbind();
        }

    private:
        std::unique_ptr<Core::Texture2D>   m_hist[2];
        std::unique_ptr<Core::FrameBuffer> m_fbo;

        uint32_t m_width  = 0;
        uint32_t m_height = 0;
        int      m_cur    = 0;
        bool     m_ready  = false;
        bool     m_primed = false;
};

} // namespace Engine
