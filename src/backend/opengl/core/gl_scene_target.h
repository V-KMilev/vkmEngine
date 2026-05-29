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
 * @brief Offscreen scene-render target: MSAA HDR + MSAA overlay + depth.
 *
 * Single FBO with three MSAA attachments sharing one depth/stencil
 * renderbuffer:
 *   - color 0: RGBA16F HDR for the lit scene (forward + skybox + SSR +
 *              all tonemapped post). resolve() blits to a single-sample
 *              RGBA16F texture the composite/AgX pass samples.
 *   - color 1: RGBA8 overlay for diagnostic passes (AABB / Grid).
 *              resolveOverlay() blits to a single-sample RGBA8 texture
 *              that composite blends OVER the tonemapped scene with
 *              sRGB encode only - so debug colors show pixel-exact
 *              regardless of exposure / tonemap / bloom.
 *   - depth/stencil: shared by both color attachments, so AABB / Grid
 *              get the same scene-geometry depth-test for free.
 *
 * bindForRender() routes draw-buffer to attachment 0; bindForOverlay()
 * routes it to attachment 1; clearOverlay() clears only attachment 1.
 *
 * Header-only to mirror gl_render_target.h; all GL machinery already lives
 * in the Core wrappers (multisample renderbuffer storage + FBO blit).
 */
class GLSceneTarget {
    public:
        GLSceneTarget() = default;
        ~GLSceneTarget() = default;

        GLSceneTarget(const GLSceneTarget& other) = delete;
        GLSceneTarget& operator=(const GLSceneTarget& other) = delete;

        GLSceneTarget(GLSceneTarget && other) = delete;
        GLSceneTarget& operator=(GLSceneTarget && other) = delete;

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

        /// True when something drew into the overlay attachment since the last
        /// clearOverlay() this frame. The composite uses this to skip the
        /// full-res MSAA overlay resolve (and the overlay blend) on the common
        /// case where no AABB/Grid/Outline/wireframe pass ran. Set by
        /// bindForOverlay(), reset by clearOverlay(); both are called only on
        /// the render thread, in pass order, so no synchronization is needed.
        bool overlayDirty() const { return m_overlayDirty; }

        /**
         * @brief Bind the multisampled FBO and set the viewport for rendering.
         *        Color attachment 0 (HDR) is the only draw target so non-AABB,
         *        non-Grid passes can clear+write without touching the overlay.
         */
        void bindForRender() const {
            if (!m_ready) return;
            m_msFbo->bind();
            const GLenum bufs[1] = { GL_COLOR_ATTACHMENT0 };
            glDrawBuffers(1, bufs);
            glViewport(0, 0, static_cast<GLsizei>(m_width), static_cast<GLsizei>(m_height));
        }

        /**
         * @brief Bind the same MSAA FBO with draw-buffer routed to the overlay
         *        attachment (color attachment 1). Diagnostic passes (AABB,
         *        Grid) write here so their pixels skip the tonemap chain in
         *        the composite while still depth-testing against the shared
         *        HDR depth attachment.
         */
        void bindForOverlay() const {
            if (!m_ready) return;
            // A caller binding the overlay attachment is about to draw into it
            // (no caller binds it speculatively); mark it dirty so composite
            // resolves + blends it this frame. Over-approximation is safe: a
            // false positive only costs an unnecessary resolve, never a
            // dropped overlay.
            m_overlayDirty = true;
            m_msFbo->bind();
            const GLenum bufs[2] = { GL_NONE, GL_COLOR_ATTACHMENT1 };
            glDrawBuffers(2, bufs);
            glViewport(0, 0, static_cast<GLsizei>(m_width), static_cast<GLsizei>(m_height));
        }

        /**
         * @brief Clear the overlay attachment (color attachment 1) to fully
         *        transparent so composite blends nothing where no diagnostic
         *        pass drew. Idempotent.
         */
        void clearOverlay() const {
            // Reset the per-frame dirty flag here (the forward opaque phase
            // clears the overlay once per frame, before any overlay pass runs).
            m_overlayDirty = false;
            if (!m_ready) return;
            m_msFbo->bind();
            const GLenum bufs[2] = { GL_NONE, GL_COLOR_ATTACHMENT1 };
            glDrawBuffers(2, bufs);
            const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            glClearBufferfv(GL_COLOR, 1, zero);
        }

        /**
         * @brief Resolve MSAA into the single-sample texture.
         *
         * Call once after the scene passes and before sampling the result.
         */
        void resolve() const {
            if (!m_ready) return;
            // Resolve attachment 0 (HDR scene).
            glBindFramebuffer(GL_READ_FRAMEBUFFER, m_msFbo->getID());
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolveFbo->getID());
            const GLenum drawHDR[1] = { GL_COLOR_ATTACHMENT0 };
            glDrawBuffers(1, drawHDR);
            glBlitFramebuffer(0, 0, static_cast<int>(m_width), static_cast<int>(m_height),
                              0, 0, static_cast<int>(m_width), static_cast<int>(m_height),
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        /**
         * @brief Resolve the overlay attachment to its single-sample texture.
         */
        void resolveOverlay() const {
            if (!m_ready || !m_overlayResolveFbo) return;
            glBindFramebuffer(GL_READ_FRAMEBUFFER, m_msFbo->getID());
            glReadBuffer(GL_COLOR_ATTACHMENT1);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_overlayResolveFbo->getID());
            const GLenum drawOv[1] = { GL_COLOR_ATTACHMENT0 };
            glDrawBuffers(1, drawOv);
            glBlitFramebuffer(0, 0, static_cast<int>(m_width), static_cast<int>(m_height),
                              0, 0, static_cast<int>(m_width), static_cast<int>(m_height),
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        /**
         * @brief Bind the resolved HDR color texture to a sampler slot.
         */
        void bindResolvedColor(uint32_t slot) const {
            if (m_resolveColor) m_resolveColor->bindSlot(slot);
        }

        /**
         * @brief Bind the resolved overlay color texture to a sampler slot
         *        (rgba: rgb = diagnostic colour in linear, a = coverage).
         */
        void bindResolvedOverlay(uint32_t slot) const {
            if (m_overlayResolve) m_overlayResolve->bindSlot(slot);
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

        /// FBO that owns the MSAA HDR + overlay + shared depth attachments.
        /// Used as a depth-blit source for the single-sample OIT FBO.
        GLuint msFboId() const {
            return m_msFbo ? m_msFbo->getID() : 0;
        }

        uint32_t width()  const { return m_width; }
        uint32_t height() const { return m_height; }

    private:
        void createAttachments() {
            GLint maxSamples = 1;
            glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
            const int samples = std::clamp(MSAA_SAMPLES, 1, static_cast<int>(maxSamples));

            // Multisampled scene target: float color + packed depth/stencil
            // + RGBA8 overlay (diagnostic colour, written by AABB/Grid only).
            m_msColor = std::make_unique<Core::RenderBuffer>();
            m_msColor->storageMultisample(samples, GL_RGBA16F,
                static_cast<int32_t>(m_width), static_cast<int32_t>(m_height));

            m_msOverlay = std::make_unique<Core::RenderBuffer>();
            m_msOverlay->storageMultisample(samples, GL_RGBA8,
                static_cast<int32_t>(m_width), static_cast<int32_t>(m_height));

            m_msDepth = std::make_unique<Core::RenderBuffer>();
            m_msDepth->storageMultisample(samples, GL_DEPTH24_STENCIL8,
                static_cast<int32_t>(m_width), static_cast<int32_t>(m_height));

            m_msFbo = std::make_unique<Core::FrameBuffer>();
            m_msFbo->bind();
            m_msFbo->attachRenderBuffer(GL_COLOR_ATTACHMENT0,        m_msColor->getID());
            m_msFbo->attachRenderBuffer(GL_COLOR_ATTACHMENT1,        m_msOverlay->getID());
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

            // Overlay resolve target (RGBA8, single-sample). Sampled by the
            // composite as a "draw this colour as-is, post-tonemap" overlay.
            Core::Texture2DParams oparams = params;
            oparams.internalFormat = GL_RGBA8;
            oparams.type           = GL_UNSIGNED_BYTE;
            m_overlayResolve = std::make_unique<Core::Texture2D>("hdr_overlay_resolve", oparams);

            m_overlayResolveFbo = std::make_unique<Core::FrameBuffer>();
            m_overlayResolveFbo->bind();
            m_overlayResolveFbo->attachTexture2D(GL_COLOR_ATTACHMENT0, m_overlayResolve->getID());
            const bool overlayOk = m_overlayResolveFbo->isComplete();
            m_overlayResolveFbo->unbind();

            m_ready = msOk && resolveOk && overlayOk;
        }

    private:
        static constexpr int MSAA_SAMPLES = 4;  ///< Requested MSAA samples (clamped to GL_MAX_SAMPLES)

        std::unique_ptr<Core::RenderBuffer> m_msColor;
        std::unique_ptr<Core::RenderBuffer> m_msOverlay;
        std::unique_ptr<Core::RenderBuffer> m_msDepth;
        std::unique_ptr<Core::FrameBuffer>  m_msFbo;

        std::unique_ptr<Core::Texture2D>    m_resolveColor;
        std::unique_ptr<Core::FrameBuffer>  m_resolveFbo;

        std::unique_ptr<Core::Texture2D>    m_overlayResolve;
        std::unique_ptr<Core::FrameBuffer>  m_overlayResolveFbo;

        uint32_t m_width  = 0;
        uint32_t m_height = 0;
        bool     m_ready  = false;

        /// Did any pass draw into the overlay attachment this frame? See
        /// overlayDirty(). Mutable: toggled from const bind/clear helpers.
        mutable bool m_overlayDirty = false;
};

} // namespace Engine
