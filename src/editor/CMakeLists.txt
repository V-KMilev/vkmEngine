# vkm_editor - ImGui panels, gizmos, overlays, and scene I/O controllers.
file(GLOB_RECURSE VKM_EDITOR_SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(vkm_editor STATIC ${VKM_EDITOR_SOURCES})

target_include_directories(vkm_editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vkm_editor PUBLIC
    vkm_core
    vkm_tools
    vkm_cook           # model import + asset cooking (editor-only heavy deps)
    imgui
    vkm_gl
)
target_link_libraries(vkm_editor PRIVATE
    vkm_render         # editor_system uses backend()/renderPreview(); headers only forward-declare it
    vkm_warnings
    vkm_build_info
)
