#include "editor/gl_editor_backend.h"

#include <string>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "vkm_opengl/core/version.h"

#include "vkm_platform/glfw_api.h"
#include "vkm_platform/window/window_manager.h"

namespace Vkm::Engine {

void GLEditorBackend::initImGui(Platform::WindowManager& window) {
    ImGui_ImplGlfw_InitForOpenGL(window.getWindowContext(), true);
    ImGui_ImplOpenGL3_Init(("#version " + std::to_string(Vkm::OpenGL::GLSL_VERSION)).c_str());
}

void GLEditorBackend::beginImGuiFrame() {
    ImGui_ImplOpenGL3_NewFrame();
}

void GLEditorBackend::renderImGui(ImDrawData* data) {
    ImGui_ImplOpenGL3_RenderDrawData(data);

    // Panels dragged out of the main window: each draws into its own window, whose context it
    // makes current, so the main one is made current again after.
    if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
        GLFWwindow* main = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(main);
    }
}

void GLEditorBackend::shutdownImGui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
}

} // namespace Vkm::Engine
