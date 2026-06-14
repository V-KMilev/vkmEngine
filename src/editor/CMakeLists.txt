# EngineEditor - ImGui panels, gizmos, overlays, and scene I/O controllers.
file(GLOB_RECURSE ENGINEEDITOR_SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(EngineEditor STATIC ${ENGINEEDITOR_SOURCES})

target_include_directories(EngineEditor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(EngineEditor PUBLIC
    EngineCore
    EngineTools
    imgui
    vkmGL
)
target_link_libraries(EngineEditor PRIVATE
    vkm_warnings
    BuildInfo
)
