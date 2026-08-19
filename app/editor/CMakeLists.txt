# vkm_editor - engine + in-process editor (ImGui panels, gizmos, scene I/O
# dialogs). The target carries an _app suffix its OUTPUT_NAME drops: the editor
# library is already called vkm_editor, and CMake target names must be unique
# even though vkm_editor and libvkm_editor.a never collide on disk.
add_executable(vkm_editor_app main.cpp)

set_target_properties(vkm_editor_app PROPERTIES OUTPUT_NAME vkm_editor)

# The app drives the engine directly, so it links the engine libs itself.
target_link_libraries(vkm_editor_app PRIVATE
    vkm_core
    vkm_render
    vkm_tools
    vkm_cook
    BackendOpenGL
    vkm_editor
    BuildInfo
    vkm_warnings
)

target_include_directories(vkm_editor_app PRIVATE ${CMAKE_SOURCE_DIR})
