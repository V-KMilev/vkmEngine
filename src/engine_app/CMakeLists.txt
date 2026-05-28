# ============================================================================
# Engine Application Bootstrap
# Wires the standard engine-tier systems + render pipeline + default scene
# into an Engine instance. Linked by both the editor and runtime binaries
# so the only thing differing between them is the editor add-on.
# ============================================================================
file(GLOB_RECURSE ENGINEAPP_SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(EngineApp STATIC ${ENGINEAPP_SOURCES})

target_include_directories(EngineApp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/vkmEngine/engine_app>
)

target_link_libraries(EngineApp PUBLIC
    EngineCore
    EngineRendering
    EngineTools
    BackendOpenGL
)

target_link_libraries(EngineApp PRIVATE BuildInfo)
