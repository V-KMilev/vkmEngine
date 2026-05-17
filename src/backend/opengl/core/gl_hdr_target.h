#pragma once

#include <memory>
#include <cstdint>
#include <algorithm>

#include <GL/glew.h>

#include "gl_frame_buffer.h"
#include "gl_render_buffer.h"
#include "gl_texture.h"
#include "gl_blit.h"

namespace Engine {

/**
 * @brief Offscreen HDR scene target with MSAA, plus a resolved sample source.
 *
 * The whole scene (opaque, grid, debug) renders into a multisampled float
 * color buffer so light is never clamped before tone mapping. After the
 * scene passes, resolve() blits the MSAA buffer down into a single-sample
 * RGBA16F texture that the composite/AgX pass samples.
 *
 * Header-only to mirror gl_render_target.h; all GL machinery already lives
 * in the Core wrappers (multisample renderbuffer storage + FBO blit).
 */
class GLHdrTarget {
    public:
        GLHdrTarget() = default;
        ~GLHdrTarget() = default;

        GLHdrTarget(const GLHdrTarget& other) = delete;
        GLHdrTarget& operator=(const GLHdrTarget& other) = delete;

        GLHdrTarget(GLHdrTarget && other) = delete;
        GLHdrTarget& operator=(GLHdrTarget && other) = delete;

    public:
        /**
         * @brief (Re)allocate all attachments for a new framebuffer size.
         */
        void resize(uint32_t width, uint32_t height) {
            if (width == 0 || height == 0) return;
            if (m_ready && width == m_width && height == m_height) return;
            m_width  = width;
            m_height = height;
            createAttachments();
        }

        bool isReady() const { return m_ready; }

        /**
         * @brief Bind the multisampled FBO and set the viewport for rendering.
         */
        void bindForRender() const {
            if (!m_ready) return;
            m_msFbo->bind();
            glViewport(0, 0, static_cast<GLsizei>(m_width), static_cast<GLsizei>(m_height));
        }

        /**
         * @brief Resolve MSAA into the single-sample texture.
         *
         * Call once after the scene passes and before sampling the result.
         */
        void resolve() const {
            if (!m_ready) return;
            Core::blitColor(m_msFbo->getID(), m_resolveFbo->getID(),
                static_cast<int>(m_width), static_cast<int>(m_height),
                static_cast<int>(m_width), static_cast<int>(m_height), GL_NEAREST);
        }

        /**
         * @brief Bind the resolved HDR color texture to a sampler slot.
         */
        void bindResolvedColor(uint32_t slot) const {
            if (m_resolveColor) m_resolveColor->bindSlot(slot);
        }

        /// Raw GL id of the resolved single-sample HDR color texture.
        GLuint resolvedColorTexture() const {
            return m_resolveColor ? m_resolveColor->getID() : 0;
        }

        /// FBO that owns the resolved color texture (a pass may blit into it
        /// to substitute a post-processed scene for downstream passes).
        GLuint resolveFboId() const {
            return m_resolveFbo ? m_resolveFbo->getID() : 0;
        }

        uint32_t width()  const { return m_width; }
        uint32_t height() const { return m_height; }

    private:
        void createAttachments() {
            GLint maxSamples = 1;
            glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
            const int samples = std::clamp(MSAA_SAMPLES, 1, static_cast<int>(maxSamples));

            // Multisampled scene target: float color + packed depth/stencil.
            m_msColor = std::make_unique<Core::RenderBuffer>();
            m_msColor->storageMultisample(samples, GL_RGBA16F,
                static_cast<int32_t>(m_width), static_cast<int32_t>(m_height));

            m_msDepth = std::make_unique<Core::RenderBuffer>();
            m_msDepth->storageMultisample(samples, GL_DEPTH24_STENCIL8,
                static_cast<int32_t>(m_width), static_cast<int32_t>(m_height));

            m_msFbo = std::make_unique<Core::FrameBuffer>();
            m_msFbo->bind();
            m_msFbo->attachRenderBuffer(GL_COLOR_ATTACHMENT0,        m_msColor->getID());
            m_msFbo->attachRenderBuffer(GL_DEPTH_STENCIL_ATTACHMENT, m_msDepth->getID());
            const bool msOk = m_msFbo->isComplete();
            m_msFbo->unbind();

            // Single-sample resolve target sampled by the composite pass.
            Core::Texture2DParams params;
            params.width           = m_width;
            params.height          = m_height;
            params.internalFormat  = GL_RGBA16F;
            params.format          = GL_RGBA;
            params.type            = GL_FLOAT;
            params.wrapS           = Core::TextureWrap::ClampToEdge;
            params.wrapT           = Core::TextureWrap::ClampToEdge;
            params.minFilter       = Core::TextureMinFilter::Linear;
            params.magFilter       = Core::TextureMagFilter::Linear;
            params.generateMipmaps = false;
            m_resolveColor = std::make_unique<Core::Texture2D>("hdr_resolve_color", params);

            m_resolveFbo = std::make_unique<Core::FrameBuffer>();
            m_resolveFbo->bind();
            m_resolveFbo->attachTexture2D(GL_COLOR_ATTACHMENT0, m_resolveColor->getID());
            const bool resolveOk = m_resolveFbo->isComplete();
            m_resolveFbo->unbind();

            m_ready = msOk && resolveOk;
        }

    private:
        static constexpr int MSAA_SAMPLES = 4;  ///< Requested MSAA samples (clamped to GL_MAX_SAMPLES)

        std::unique_ptr<Core::RenderBuffer> m_msColor;
        std::unique_ptr<Core::RenderBuffer> m_msDepth;
        std::unique_ptr<Core::FrameBuffer>  m_msFbo;

        std::unique_ptr<Core::Texture2D>    m_resolveColor;
        std::unique_ptr<Core::FrameBuffer>  m_resolveFbo;

        uint32_t m_width  = 0;
        uint32_t m_height = 0;
        bool     m_ready  = false;
};

} // namespace Engine
