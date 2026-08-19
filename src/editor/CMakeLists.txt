# EngineEditor - ImGui panels, gizmos, overlays, and scene I/O controllers.
file(GLOB_RECURSE ENGINEEDITOR_SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(EngineEditor STATIC ${ENGINEEDITOR_SOURCES})

target_include_directories(EngineEditor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(EngineEditor PUBLIC
    EngineCore
    EngineTools
    EngineCooker       # model import + asset cooking (editor-only heavy deps)
    imgui
    vkm_gl
)
target_link_libraries(EngineEditor PRIVATE
    EngineRendering    # editor_system uses backend()/renderPreview(); headers only forward-declare it
    vkm_warnings
    BuildInfo
)
