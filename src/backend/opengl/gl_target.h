#pragma once

#include <cstdint>
#include <memory>

#include "gl_frame_buffer.h"

namespace Core {
    class Context;
    class Texture2D;
}

namespace Engine {

/**
 * @brief An off-screen render target - an HDR colour texture + sampleable depth
 *        in one FBO, with an optional second colour attachment (a G-buffer of
 *        view normal + roughness + metalness) for the screen-space passes.
 *
 * Passes draw into one of these instead of the backbuffer; later passes sample
 * its colour/depth/G-buffer (SSR, GTAO, refraction scene-grab, motion blur).
 * Call enableGBuffer() once before the first resize() to add the G-buffer
 * attachment. The depth attachment is a sampleable texture so those passes can
 * reconstruct position from it.
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
        /// Add a second colour attachment (view normal + roughness + metalness).
        /// Call once before the first resize().
        void enableGBuffer() { m_hasGBuffer = true; }

        void resize(uint32_t width, uint32_t height);

        /// Bind for rendering into colour 0 (the common case: skybox, forward,
        /// SSR resolve). Sets the viewport and the draw buffer to colour 0.
        /// Non-const: it mutates GL draw-buffer state.
        void bind(const Core::Context& gl);

        /// Bind for the G-buffer prepass: draws into colour 1 only.
        void bindGBufferPass(const Core::Context& gl);

        /// Clear the whole target for a new frame (all colour attachments +
        /// depth). The first pass to touch the target calls this.
        void clearForFrame(const Core::Context& gl);

        void bindColor(uint32_t slot) const;
        void bindDepth(uint32_t slot) const;
        void bindGBuffer(uint32_t slot) const;

        /// Copy @p src's colour into this target (both viewport-sized). Used to
        /// snapshot the scene for refraction and to resolve SSR back into it.
        void blitColorFrom(const GLTarget& src);

    private:
        uint32_t m_width  = 0;
        uint32_t m_height = 0;
        bool     m_hasGBuffer = false;

        Core::FrameBuffer                m_fbo;
        std::unique_ptr<Core::Texture2D> m_color;
        std::unique_ptr<Core::Texture2D> m_depth;
        std::unique_ptr<Core::Texture2D> m_gbuffer;
};

} // namespace Engine
