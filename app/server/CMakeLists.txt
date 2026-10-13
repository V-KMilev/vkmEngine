# vkm_server - hosts a game and referees it, with no window and no render
# backend.
#
# It still needs glfw's shared library present, because vkm_core links it and
# window_manager.cpp calls into it.
add_executable(vkm_server_app main.cpp)

set_target_properties(vkm_server_app PROPERTIES OUTPUT_NAME vkm_server)

target_link_libraries(vkm_server_app PRIVATE
    vkm_core
    vkm_render
    vkm_tools
    vkm_build_info
    vkm_warnings
)

target_include_directories(vkm_server_app PRIVATE ${CMAKE_SOURCE_DIR})
