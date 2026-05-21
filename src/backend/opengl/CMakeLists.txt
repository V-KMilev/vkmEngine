# ============================================================================
# OpenGL Backend Library
# ============================================================================
file(GLOB_RECURSE BACKENDOPENGL_SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(BackendOpenGL STATIC ${BACKENDOPENGL_SOURCES})

target_include_directories(BackendOpenGL PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/vkmEngine/backend/opengl>
)

target_link_libraries(BackendOpenGL PUBLIC
    vkmGL
    EngineRendering
    EngineTools
)
