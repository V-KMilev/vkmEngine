# vkm_cook - bakes a project's assets with no window and no GL context, so a
# release build needs neither a GUI step nor a GPU. Links the importers the
# runtime deliberately excludes.
add_executable(vkm_cook_app main.cpp)

set_target_properties(vkm_cook_app PROPERTIES OUTPUT_NAME vkm_cook)

target_link_libraries(vkm_cook_app PRIVATE
    vkm_core
    vkm_tools
    vkm_cook
    vkm_build_info
    vkm_warnings
)

target_include_directories(vkm_cook_app PRIVATE ${CMAKE_SOURCE_DIR})
