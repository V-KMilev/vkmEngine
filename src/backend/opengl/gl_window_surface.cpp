#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_window_surface.h"

#include <stdexcept>

#include "vkm_log/logger.h"
#include "vkm_opengl/core/debug.h"
#include "vkm_opengl/core/load.h"
#include "vkm_opengl/core/version.h"
#include "vkm_opengl/shader/shader_base.h"

#include "vkm_platform/glfw_api.h"

namespace Vkm::Engine {

void GLWindowSurface::hint() const {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, Vkm::OpenGL::MAJOR_VERSION);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, Vkm::OpenGL::MINOR_VERSION);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
}

std::string GLWindowSurface::describe() const {
    return "OpenGL " + std::to_string(Vkm::OpenGL::MAJOR_VERSION) + "."
        + std::to_string(Vkm::OpenGL::MINOR_VERSION) + " core";
}

void GLWindowSurface::attach(GLFWwindow* window) {
    // Required before loading OpenGL and glfwSwapInterval below.
    glfwMakeContextCurrent(window);

    if (const Vkm::OpenGL::Loaded loaded = Vkm::OpenGL::load(); !loaded.ok) {
        LOG_ERROR("Failed to load OpenGL: %s", loaded.reason.c_str());
        throw std::runtime_error("Failed to load OpenGL: " + loaded.reason);
    }

#if !VKM_SHIPPING
    // A shipped game asks for no debug output, as the driver's checks would cost every frame.
    Vkm::OpenGL::enableGLDebugLogging();
#endif
    // Every program built after this compiles on the driver's threads; the backend's startup
    // waits for them behind the chrome rather than one at a time with the window frozen.
    if (!Vkm::OpenGL::ShaderBase::compileInParallel()) LOG_INFO("This driver compiles shaders one at a time");
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
