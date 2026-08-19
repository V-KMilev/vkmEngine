# vkm_backend_gl - the OpenGL implementation behind the RenderBackend interface.
file(GLOB_RECURSE VKM_BACKEND_GL_SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(vkm_backend_gl STATIC ${VKM_BACKEND_GL_SOURCES})

target_include_directories(vkm_backend_gl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vkm_backend_gl
    PUBLIC
        vkm_gl
        vkm_render
        vkm_tools
    PRIVATE
        vkm_warnings
)
