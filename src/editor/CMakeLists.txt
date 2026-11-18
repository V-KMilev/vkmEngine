# vkm_editor - ImGui panels, gizmos, overlays, and scene I/O controllers.
file(GLOB_RECURSE VKM_EDITOR_SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(vkm_editor STATIC ${VKM_EDITOR_SOURCES})

target_include_directories(vkm_editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vkm_editor PUBLIC
    vkm_core
    vkm_loader
    vkm_pipeline       # model import + asset cooking (editor-only heavy deps)
    imgui
)
target_link_libraries(vkm_editor PRIVATE
    vkm_render         # RenderSystem::backend(), the way to the editor hooks; headers only forward-declare it
    glfw               # the window's scale and focus, and the windows panels are dragged out into
    vkm_warnings
    vkm_build_info
)
add_dependencies(vkm_editor vkm_build_stamp)  # the About box shows build_stamp.h
