# vkm_cook - bakes a project's assets with no window and no GL context, so a
# release build needs no GUI step and no GPU. Links the tools (Assimp, image
# decode) the runtime deliberately excludes. The target carries an _app suffix
# its OUTPUT_NAME drops, the cooker library having claimed vkm_cook already.
add_executable(vkm_cook_app main.cpp)

set_target_properties(vkm_cook_app PROPERTIES OUTPUT_NAME vkm_cook)

target_link_libraries(vkm_cook_app PRIVATE
    vkm_core
    vkm_tools
    vkm_cook
    BuildInfo
    vkm_warnings
)

target_include_directories(vkm_cook_app PRIVATE ${CMAKE_SOURCE_DIR})
