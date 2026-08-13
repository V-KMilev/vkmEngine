#pragma once

#include <cstdint>
#include <memory>

#include <GL/glew.h>

#include "gl_frame_buffer.h"

namespace Core {
    class Context;
    class Texture2D;
}

namespace Engine {

/**
 * @brief Screen-space mask target - one small-format texture in its own FBO.
 *
 * Backs the GTAO factor (RGBA16F via setFormat, to carry a packed bent normal)
 * and the sun contact-shadow mask (the default lone R16F channel); a pass
 * renders its mask into this and the forward pass samples it. Deliberately
 * lighter than GLTarget: no depth, no G-buffer, no HDR colour - just the mask,
 * linear-filtered so a future half-res variant can upsample cleanly.
 */
class GLMaskTarget {
    public:
        GLMaskTarget();
        ~GLMaskTarget();

        GLMaskTarget(const GLMaskTarget& other) = delete;
        GLMaskTarget& operator=(const GLMaskTarget& other) = delete;

        GLMaskTarget(GLMaskTarget && other) = delete;
        GLMaskTarget& operator=(GLMaskTarget && other) = delete;

    public:
        /**
         * @brief Override the texture format (default R16F, a lone factor).
         *
         * Call before the first resize(). GTAO uses RGBA16F to carry a packed
         * bent normal alongside the occlusion factor; a plain mask keeps R16F.
         */
        void setFormat(GLenum internalFormat, GLenum format);

        void resize(uint32_t width, uint32_t height);

        /**
         * @brief Bind for rendering the AO factor (sets the FBO + viewport). Non-const:
         * it mutates GL draw-buffer state.
         */
        void bind(const Core::Context& gl);

        /**
         * @brief Bind the AO texture to @p slot for the forward pass to sample.
         *
         * @param slot Texture unit the forward shader reads the AO factor from.
         */
        void bindTexture(uint32_t slot) const;

    private:
        uint32_t m_width  = 0;
        uint32_t m_height = 0;
        GLenum   m_internalFormat = GL_R16F;
        GLenum   m_format         = GL_RED;

        Core::FrameBuffer                m_fbo;
        std::unique_ptr<Core::Texture2D> m_tex;
};

} // namespace Engine
