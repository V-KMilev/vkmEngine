# engine_cook - bakes a project's assets with no window and no GL context, so a
# release build needs no GUI step and no GPU. Links the tools (Assimp, image
# decode) the runtime deliberately excludes.
add_executable(engine_cook main.cpp)

target_link_libraries(engine_cook PRIVATE
    vkm_core
    vkm_tools
    vkm_cook
    BuildInfo
    vkm_warnings
)

target_include_directories(engine_cook PRIVATE ${CMAKE_SOURCE_DIR})
