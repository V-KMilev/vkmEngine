#pragma once

#include <cstdint>
#include <memory>

#include "gl_frame_buffer.h"
#include "gl_render_buffer.h"

namespace Core {
    class Context;
    class Texture2D;
}

namespace Engine {

/**
 * @brief An off-screen render target - an HDR color texture + depth in one FBO.
 *
 * Passes draw into one of these instead of the backbuffer; later passes sample
 * its color. resize() reallocates to the viewport; the depth attachment is a
 * renderbuffer, written for depth testing but never sampled.
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
        void resize(uint32_t width, uint32_t height);
        void bind(const Core::Context& gl) const;
        void bindColor(uint32_t slot) const;

    private:
        uint32_t m_width  = 0;
        uint32_t m_height = 0;

        Core::FrameBuffer                m_fbo;
        std::unique_ptr<Core::Texture2D> m_color;
        Core::RenderBuffer               m_depth;
};

} // namespace Engine
