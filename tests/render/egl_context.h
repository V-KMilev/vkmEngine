#pragma once

#include <string>

namespace Vkm::Test {

/**
 * @brief A real OpenGL context with no window and no display.
 *
 * The engine's own context comes from GLFW, which needs a platform - X11,
 * Wayland - and a build machine has neither. That is why `src/backend/opengl`
 * has never had a test: not because the code resists one, but because nothing
 * could hand it a context to run against.
 *
 * EGL can, through `EGL_EXT_platform_device`: it opens the GPU directly and
 * makes a context current with no surface at all. This is a *second* way to get
 * a context and it exists only here, in the tests - the engine still goes
 * through GLFW, because a game needs the window that comes with it.
 *
 * Absence is not failure. A machine with no EGL, no device, or a driver too old
 * for a 4.5 core context reports `available() == false` and the suite says it
 * skipped rather than failing, so the tests stay runnable where there is no GPU.
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

        /// Why there is no context, for the line the suite prints when it skips.
        const std::string& reason() const { return m_reason; }

        /// GL_VERSION as the driver reports it, empty when unavailable.
        const std::string& version() const { return m_version; }

        /// GL_RENDERER, which is the line worth printing: it names the GPU.
        const std::string& renderer() const { return m_renderer; }

    private:
        bool        m_available = false;
        std::string m_reason;
        std::string m_version;
        std::string m_renderer;

        void*       m_display = nullptr;   ///< EGLDisplay, opaque so EGL stays out of this header
        void*       m_context = nullptr;   ///< EGLContext, likewise
};

} // namespace Vkm::Test
