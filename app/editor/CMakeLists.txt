# vkm_editor - the engine with the in-process editor (ImGui panels, gizmos,
# scene I/O dialogs) on top of it.
add_executable(vkm_editor_app main.cpp)

# On Windows a windowed program: opened from Explorer or a shortcut, no terminal comes up behind
# it (bootHost writes to the one it was started from, if any).
set_target_properties(vkm_editor_app PROPERTIES OUTPUT_NAME vkm_editor WIN32_EXECUTABLE ON)

target_link_libraries(vkm_editor_app PRIVATE
    vkm_core
    vkm_render
    vkm_loader
    vkm_pipeline
    vkm_backend_gl_editor   # its backend: the runtime's, with ImGui's OpenGL renderer (app/backend_choice.h)
    vkm_editor
    vkm_build_info
    vkm_warnings
)

target_include_directories(vkm_editor_app PRIVATE ${CMAKE_SOURCE_DIR})
