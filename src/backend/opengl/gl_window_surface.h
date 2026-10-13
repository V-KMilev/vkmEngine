#pragma once

#include <string>

#include "platform/window/window_surface.h"

namespace Vkm::Engine {

/// The OpenGL this backend asks the window for; GLSL's "#version" follows it.
inline constexpr int OPENGL_MAJOR_VERSION = 4;
inline constexpr int OPENGL_MINOR_VERSION = 3;
inline constexpr int OPENGL_GLSL_VERSION  = OPENGL_MAJOR_VERSION * 100 + OPENGL_MINOR_VERSION * 10;

/**
 * @brief The window as GLBackend draws into it: a core context of OPENGL_MAJOR/MINOR_VERSION.
 *
 * Attaching makes the context current, loads GL through GLEW and turns GL's debug output on, so
 * GLBackend::init finds a live context; presenting swaps the window's buffers.
 */
class GLWindowSurface : public WindowSurface {
    public:
        GLWindowSurface()           = default;
        ~GLWindowSurface() override = default;

        GLWindowSurface(const GLWindowSurface& other) = delete;
        GLWindowSurface& operator=(const GLWindowSurface& other) = delete;

        GLWindowSurface(GLWindowSurface && other) = delete;
        GLWindowSurface& operator=(GLWindowSurface && other) = delete;

    public:
        void        hint() const override;
        std::string describe() const override;
        void        attach(GLFWwindow* window) override;
        void        present(GLFWwindow* window) override;
        void        setVSync(GLFWwindow* window, bool enabled) override;
};

} // namespace Vkm::Engine
