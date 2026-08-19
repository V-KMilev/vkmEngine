# vkm_runtime - bare engine executable, no editor. Foundation for a future
# game binary. The target carries an _app suffix its OUTPUT_NAME drops, so the
# three hosts read alike in the build files and ship under the plain names.
add_executable(vkm_runtime_app main.cpp)

set_target_properties(vkm_runtime_app PROPERTIES OUTPUT_NAME vkm_runtime)

# The app drives the engine directly, so it links the engine libs itself.
target_link_libraries(vkm_runtime_app PRIVATE
    vkm_core
    vkm_render
    vkm_tools
    vkm_backend_gl
    vkm_build_info
    vkm_warnings
)

target_include_directories(vkm_runtime_app PRIVATE ${CMAKE_SOURCE_DIR})
