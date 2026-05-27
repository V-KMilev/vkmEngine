# ============================================================================
# Engine Editor Library
# ============================================================================
file(GLOB_RECURSE ENGINEEDITOR_SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(EngineEditor STATIC ${ENGINEEDITOR_SOURCES})

target_include_directories(EngineEditor PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/vkmEngine/editor>
)

target_link_libraries(EngineEditor PUBLIC
    EngineCore
    EngineTools
    BackendOpenGL  # variant-cache stats panel reaches into GLView
    imgui
    vkmGL
)
