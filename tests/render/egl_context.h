#pragma once

#include <string>

namespace Vkm::Test {

/**
 * @brief A real OpenGL context with no window and no display.
 *
 * The engine's context comes from GLFW, which needs X11 or Wayland; a build
 * machine has neither. EGL's `EGL_EXT_platform_device` opens the GPU directly and
 * makes a context current with no surface. Only the tests use it.
 *
 * Absence is not failure: with no EGL, no device or too old a driver,
 * `available() == false` and the suite skips, so the tests run without a GPU.
 */
class GLContext {
    public:
        GLContext();
        ~GLContext();

        GLContext(const GLContext& other) = delete;
        GLContext& operator=(const GLContext& other) = delete;

        GLContext(GLContext && other) = delete;
        GLContext& operator=(GLContext && other) = delete;

    public:
        /// True when a context is current and GL calls will do something.
        bool available() const { return m_available; }

        /// Why there is no context, for the suite's skip line.
        const std::string& reason() const { return m_reason; }

        /// GL_VERSION, empty when unavailable.
        const std::string& version() const { return m_version; }

        /// GL_RENDERER: names the GPU.
        const std::string& renderer() const { return m_renderer; }

        /**
         * @brief Give the context a default framebuffer: an off-screen pbuffer.
         *
         * A whole frame ends on the default framebuffer (see
         * GLPass::bindBackbufferViewport), which a surfaceless context lacks.
         *
         * @param width  Surface width in pixels.
         * @param height Surface height in pixels.
         * @return False when the device offers no pbuffer; the context stays as it was.
         */
        bool attachSurface(int width, int height);

    private:
        bool        m_available = false;
        std::string m_reason;
        std::string m_version;
        std::string m_renderer;

        void*       m_display = nullptr;   ///< EGLDisplay, opaque so EGL stays out of this header
        void*       m_context = nullptr;   ///< EGLContext
        void*       m_surface = nullptr;   ///< EGLSurface once attachSurface has made one
};

} // namespace Vkm::Test
