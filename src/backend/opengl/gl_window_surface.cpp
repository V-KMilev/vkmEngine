#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_window_surface.h"

#include <stdexcept>

#include <GL/glew.h>

#include "gl_debug.h"
#include "logger.h"

#include "platform/window/glfw_include.h"

namespace Vkm::Engine {

void GLWindowSurface::hint() const {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, OPENGL_MAJOR_VERSION);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, OPENGL_MINOR_VERSION);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
}

std::string GLWindowSurface::describe() const {
    return "OpenGL " + std::to_string(OPENGL_MAJOR_VERSION) + "." + std::to_string(OPENGL_MINOR_VERSION)
        + " core";
}

void GLWindowSurface::attach(GLFWwindow* window) {
    // Required before glewInit and glfwSwapInterval below.
    glfwMakeContextCurrent(window);

    if (const GLenum glewError = glewInit(); glewError != GLEW_OK) {
        LOG_ERROR(
            "Failed to initialize GLEW: %s",
            reinterpret_cast<const char*>(glewGetErrorString(glewError))
        );
        throw std::runtime_error("Failed to initialize GLEW");
    }

    // Asynchronous: synchronous debug output keeps the driver from running ahead of the caller.
    Vkm::GL::enableGLDebugLogging(false);
    glfwSwapInterval(0);
}

void GLWindowSurface::present(GLFWwindow* window) {
    glfwSwapBuffers(window);
}

void GLWindowSurface::setVSync(GLFWwindow* window, bool enabled) {
    glfwMakeContextCurrent(window);
    glfwSwapInterval(enabled ? 1 : 0);
}

} // namespace Vkm::Engine
