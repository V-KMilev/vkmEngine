# vkm_runtime - the engine with no editor, which is what a shipped game runs.
add_executable(vkm_runtime_app main.cpp)

set_target_properties(vkm_runtime_app PROPERTIES OUTPUT_NAME vkm_runtime)

target_link_libraries(vkm_runtime_app PRIVATE
    vkm_core
    vkm_render
    vkm_tools
    vkm_backend_gl
    vkm_build_info
    vkm_warnings
)

target_include_directories(vkm_runtime_app PRIVATE ${CMAKE_SOURCE_DIR})
