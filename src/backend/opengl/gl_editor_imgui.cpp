#include "gl_backend.h"

#include <string>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "gl_window_surface.h"
#include "platform/window/glfw_include.h"

namespace Vkm::Engine {

void GLBackend::initImGui(GLFWwindow* window) {
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(("#version " + std::to_string(OPENGL_GLSL_VERSION)).c_str());
}

void GLBackend::beginImGuiFrame() {
    ImGui_ImplOpenGL3_NewFrame();
}

void GLBackend::renderImGui(ImDrawData* data) {
    ImGui_ImplOpenGL3_RenderDrawData(data);

    // Panels dragged out of the main window: each draws into its own window, whose context it
    // makes current, so the main one is made current again after.
    if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
        GLFWwindow* main = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(main);
    }
    // A window closed this frame drew its preview before it closed; the texture outlived that.
    m_preview.freeReleased();
}

void GLBackend::shutdownImGui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
}

} // namespace Vkm::Engine
