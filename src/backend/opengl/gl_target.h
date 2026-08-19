#pragma once

#include <cstdint>
#include <memory>

#include "gl_frame_buffer.h"

namespace Vkm::GL {
    class Context;
    class Texture2D;
    class RenderBuffer;
}

namespace Engine {

/**
 * @brief An off-screen render target - an HDR colour texture + sampleable depth
 *        in one FBO, with an optional second colour attachment (a G-buffer of
 *        view normal + roughness + metalness) for the screen-space passes.
 *
 * Passes draw into one of these instead of the backbuffer; later passes sample
 * its colour/depth/G-buffer (GTAO, decals, fog, DoF, refraction scene-grab).
 * Call enableGBuffer() once before the first resize() to add the G-buffer
 * attachment. The depth attachment is a sampleable texture so those passes can
 * reconstruct position from it.
 *
 * setSamples(N > 1) turns this into a render-only multisample target: its
 * attachments become renderbuffers (not sampleable) and the geometry passes
 * draw into it, then resolveColorTo / resolveGeometryTo blit-resolve it into a
 * single-sample GLTarget the screen-space passes sample. A single-sample target
 * (the default) is both drawn into and sampled directly.
 */
class GLTarget {
    public:
        GLTarget();
        ~GLTarget();

        GLTarget(const GLTarget& other) = delete;
        GLTarget& operator=(const GLTarget& other) = delete;

        GLTarget(GLTarget && other) = delete;
        GLTarget& operator=(GLTarget && other) = delete;

    public:
        /**
         * @brief Add a second colour attachment (view normal + roughness + metalness).
         * Call once before the first resize().
         */
        void enableGBuffer() { m_hasGBuffer = true; }

        /**
         * @brief Make this a colour-only target: resize() then allocates no depth
         * attachment. Call once before the first resize(), like enableGBuffer().
         *
         * For the post-chain scratch targets - the post passes depth-test
         * nothing and sample the geometry target's depth as a texture, so a
         * scratch depth buffer would be dead weight (~8 MB at 1080p).
         */
        void setColorOnly() { m_colorOnly = true; }

        /**
         * @brief Request @p samples-way multisampling. Call before the first
         * resize(); changing it later forces a reallocation on the next resize.
         *
         * @p samples == 1 keeps the single-sample, sampleable texture target.
         * @p samples > 1 makes this a render-only multisample (renderbuffer)
         * target, clamped to the driver's cap (Context::maxSamples).
         *
         * @param samples Requested per-pixel sample count.
         * @param gl      Context supplying the cached driver cap.
         */
        void setSamples(uint32_t samples, const Vkm::GL::Context& gl);

        /**
         * @brief Sample count in effect (1 = single-sample).
         *
         * A report of this target's own state and nothing more. Which target the
         * frame renders into, and whether the resolve passes do any work, are
         * both decided in the backend - from view.settings.msaaSamples and from
         * whether sceneRender and sceneHDR are the same object.
         */
        uint32_t samples() const { return m_samples; }

        void resize(uint32_t width, uint32_t height);

        /**
         * @brief Free the attachment storage (textures / renderbuffers), keeping
         * the FBO object. The next resize re-allocates. Used to reclaim the
         * multisample target's memory when MSAA is switched off.
         */
        void release();

        /**
         * @brief Bind for rendering into colour 0 (the common case: skybox, forward,
         * post blit-back). Non-const: it mutates GL draw-buffer state.
         */
        void bind(const Vkm::GL::Context& gl);

        /**
         * @brief Bind for the G-buffer prepass: draws into colour 1 only.
         *
         * @param gl Live GL context whose draw-buffer + viewport state is set.
         */
        void bindGBufferPass(const Vkm::GL::Context& gl);

        /**
         * @brief Clear the whole target for a new frame (all colour attachments +
         * depth). The first pass to touch the target calls this.
         */
        void clearForFrame(const Vkm::GL::Context& gl);

        void bindColor(uint32_t slot) const;
        void bindDepth(uint32_t slot) const;
        void bindGBuffer(uint32_t slot) const;

        /**
         * @brief Copy @p src's colour into this target (both viewport-sized). Used to
         * snapshot the scene for refraction and to blit a post pass back into it.
         */
        void blitColorFrom(const GLTarget& src);

        /**
         * @brief Blit-resolve this multisample target's colour attachment into
         * @p dst's single-sample colour texture. No-op when single-sample.
         */
        void resolveColorTo(GLTarget& dst);

        /**
         * @brief Blit-resolve this multisample target's depth - and, when
         * @p gbuffer is set, its G-buffer - into @p dst (the depth + G-buffer the
         * screen-space passes read). No-op when single-sample.
         *
         * @param dst     The single-sample target receiving the resolve.
         * @param gbuffer Whether to also resolve colour attachment 1; pass false
         *                when no downstream pass reads the resolved G-buffer.
         */
        void resolveGeometryTo(GLTarget& dst, bool gbuffer);

    private:
        uint32_t m_width   = 0;
        uint32_t m_height  = 0;
        uint32_t m_samples = 1;
        bool     m_hasGBuffer = false;
        bool     m_colorOnly  = false;

        Vkm::GL::FrameBuffer                m_fbo;

        // Single-sample (m_samples == 1): sampleable textures.
        std::unique_ptr<Vkm::GL::Texture2D> m_color;
        std::unique_ptr<Vkm::GL::Texture2D> m_depth;
        std::unique_ptr<Vkm::GL::Texture2D> m_gbuffer;

        // Multisample (m_samples > 1): render-only renderbuffers, resolved out.
        std::unique_ptr<Vkm::GL::RenderBuffer> m_colorRB;
        std::unique_ptr<Vkm::GL::RenderBuffer> m_depthRB;
        std::unique_ptr<Vkm::GL::RenderBuffer> m_gbufferRB;
};

} // namespace Engine
