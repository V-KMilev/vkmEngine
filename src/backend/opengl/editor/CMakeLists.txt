# vkm_backend_gl_editor - GLBackend with ImGui's OpenGL renderer, apart from the backend so that
# no host but the editor links ImGui.
add_library(vkm_backend_gl_editor STATIC gl_editor_backend.cpp)

target_link_libraries(vkm_backend_gl_editor
    PUBLIC
        vkm_backend_gl
        imgui_opengl3      # ImGui's OpenGL renderer, and ImGui with its GLFW glue
    PRIVATE
        glfw               # the detached windows' contexts
        vkm_warnings
)
