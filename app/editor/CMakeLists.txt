# engine_editor - engine + in-process editor (ImGui panels, gizmos, scene I/O
# dialogs).
add_executable(engine_editor main.cpp)

# The app drives the engine directly, so it links the engine libs itself.
target_link_libraries(engine_editor PRIVATE
    vkm_core
    vkm_render
    vkm_tools
    vkm_cook
    BackendOpenGL
    vkm_editor
    BuildInfo
    vkm_warnings
)

target_include_directories(engine_editor PRIVATE ${CMAKE_SOURCE_DIR})
