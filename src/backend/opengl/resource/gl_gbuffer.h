#pragma once

#include <memory>
#include <cstdint>

#include <GL/glew.h>

#include "gl_frame_buffer.h"
#include "gl_render_buffer.h"
#include "gl_texture.h"

namespace Engine {

/**
 * @brief Thin view-space G-buffer + AO target for screen-space effects.
 *
 * A single-sample MRT (view-space normal + view-space position, RGBA16F) with
 * a depth renderbuffer, plus a one-channel R16F ambient-occlusion texture the
 * GTAO pass writes and the forward PBR pass samples. The geometry MRT is
 * viewport-sized; the AO target is HALF resolution (GTAO is low frequency -
 * 4x less work) with linear filtering so the forward pass gets a free smooth
 * upsample. Rebuilt on resize. Header-only to mirror gl_scene_target.h - all GL
 * machinery is in the Core wrappers.
 */
class GLGBuffer {
    public:
        GLGBuffer() = default;
        ~GLGBuffer() = default;

        GLGBuffer(const GLGBuffer& other) = delete;
        GLGBuffer& operator=(const GLGBuffer& other) = delete;

        GLGBuffer(GLGBuffer && other) = delete;
        GLGBuffer& operator=(GLGBuffer && other) = delete;

    public:
        void resize(uint32_t width, uint32_t height) {
            if (width == 0 || height == 0) return;
            if (m_ready && width == m_width && height == m_height) return;
            m_width  = width;
            m_height = height;
            createAttachments();
        }

        bool isReady() const { return m_ready; }

        /// Bind the geometry MRT (normal + position) for the prepass.
        void bindGeometry() const {
            if (!m_ready) return;
            m_geoFbo->bind();
            const GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
            m_geoFbo->setDrawBuffers(bufs, 2);
            glViewport(0, 0, static_cast<GLsizei>(m_width), static_cast<GLsizei>(m_height));
        }

        /// Bind the single-channel (half-res) AO target for the GTAO pass.
        void bindAO() const {
            if (!m_ready) return;
            m_aoFbo->bind();
            glViewport(0, 0, static_cast<GLsizei>(m_aoWidth), static_cast<GLsizei>(m_aoHeight));
        }

        /// Bind the view-space normal G-buffer to a sampler slot.
        void bindNormal(uint32_t slot)    const { if (m_normal) m_normal->bindSlot(slot); }
        /// Bind the view-space position G-buffer to a sampler slot.
        void bindPosition(uint32_t slot)  const { if (m_pos)    m_pos->bindSlot(slot); }
        /// Bind the ambient-occlusion result to a sampler slot. The forward
        /// PBR shader gates its use on u_ssaoEnabled, so an always-bind here
        /// is correct whether or not SSAO is active.
        void bindOcclusion(uint32_t slot) const { if (m_ao)     m_ao->bindSlot(slot); }

    private:
        static Core::Texture2DParams colorParams(uint32_t w, uint32_t h, GLenum internal, GLenum fmt) {
            Core::Texture2DParams p;
            p.width = w;
            p.height = h;
            p.internalFormat = internal;
            p.format = fmt;
            p.type = GL_FLOAT;
            p.wrapS = Core::TextureWrap::ClampToEdge;
            p.wrapT = Core::TextureWrap::ClampToEdge;
            p.minFilter = Core::TextureMinFilter::Nearest;
            p.magFilter = Core::TextureMagFilter::Nearest;
            p.generateMipmaps = false;
            return p;
        }

        void createAttachments() {
            m_normal = std::make_unique<Core::Texture2D>(
                "gbuffer_view_normal", colorParams(m_width, m_height, GL_RGBA16F, GL_RGBA));
            m_pos = std::make_unique<Core::Texture2D>(
                "gbuffer_view_pos", colorParams(m_width, m_height, GL_RGBA16F, GL_RGBA));

            m_depth = std::make_unique<Core::RenderBuffer>();
            m_depth->storage(GL_DEPTH_COMPONENT24,
                static_cast<int32_t>(m_width), static_cast<int32_t>(m_height));

            m_geoFbo = std::make_unique<Core::FrameBuffer>();
            m_geoFbo->bind();
            m_geoFbo->attachTexture2D(GL_COLOR_ATTACHMENT0, m_normal->getID());
            m_geoFbo->attachTexture2D(GL_COLOR_ATTACHMENT1, m_pos->getID());
            m_geoFbo->attachRenderBuffer(GL_DEPTH_ATTACHMENT, m_depth->getID());
            const bool geoOk = m_geoFbo->isComplete();
            m_geoFbo->unbind();

            // Half-res AO with linear filtering: 4x fewer fragments and the
            // forward pass samples it as a smooth bilinear upsample.
            m_aoWidth  = m_width  > 1 ? m_width  / 2u : 1u;
            m_aoHeight = m_height > 1 ? m_height / 2u : 1u;
            Core::Texture2DParams aop = colorParams(m_aoWidth, m_aoHeight, GL_R16F, GL_RED);
            aop.minFilter = Core::TextureMinFilter::Linear;
            aop.magFilter = Core::TextureMagFilter::Linear;
            m_ao = std::make_unique<Core::Texture2D>("gbuffer_ao", aop);

            m_aoFbo = std::make_unique<Core::FrameBuffer>();
            m_aoFbo->bind();
            m_aoFbo->attachTexture2D(GL_COLOR_ATTACHMENT0, m_ao->getID());
            const bool aoOk = m_aoFbo->isComplete();
            m_aoFbo->unbind();

            m_ready = geoOk && aoOk;
        }

    private:
        std::unique_ptr<Core::Texture2D>    m_normal;
        std::unique_ptr<Core::Texture2D>    m_pos;
        std::unique_ptr<Core::RenderBuffer> m_depth;
        std::unique_ptr<Core::FrameBuffer>  m_geoFbo;

        std::unique_ptr<Core::Texture2D>   m_ao;
        std::unique_ptr<Core::FrameBuffer> m_aoFbo;

        uint32_t m_width    = 0;
        uint32_t m_height   = 0;
        uint32_t m_aoWidth  = 0;   // half of m_width  (GTAO target)
        uint32_t m_aoHeight = 0;   // half of m_height
        bool     m_ready    = false;
};

} // namespace Engine
