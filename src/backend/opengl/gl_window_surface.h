#pragma once

#include <string>

#include "vkm_platform/window/window_surface.h"

namespace Vkm::Engine {

/**
 * @brief The window as GLBackend draws into it: a core context of the OpenGL vkmOpenGL needs
 *        (Vkm::OpenGL::MAJOR_VERSION, MINOR_VERSION).
 *
 * Attaching makes the context current, loads OpenGL (Vkm::OpenGL::load) and turns GL's debug output
 * on, so GLBackend::init finds a live context; presenting swaps the window's buffers.
 */
class GLWindowSurface : public Platform::WindowSurface {
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
