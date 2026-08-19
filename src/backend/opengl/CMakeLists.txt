# BackendOpenGL - the OpenGL implementation behind the RenderBackend interface.
file(GLOB_RECURSE BACKENDOPENGL_SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(BackendOpenGL STATIC ${BACKENDOPENGL_SOURCES})

target_include_directories(BackendOpenGL PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(BackendOpenGL
    PUBLIC
        vkm_gl
        vkm_render
        vkm_tools
    PRIVATE
        vkm_warnings
)
