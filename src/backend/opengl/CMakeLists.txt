# vkm_backend_gl - the OpenGL implementation behind the RenderBackend interface.
file(GLOB_RECURSE VKM_BACKEND_GL_SOURCES CONFIGURE_DEPENDS *.cpp)
# ImGui's OpenGL renderer is a target of its own (editor/), which the editor's host alone links.
list(FILTER VKM_BACKEND_GL_SOURCES EXCLUDE REGEX "^${CMAKE_CURRENT_SOURCE_DIR}/editor/")

add_library(vkm_backend_gl STATIC ${VKM_BACKEND_GL_SOURCES})

target_include_directories(vkm_backend_gl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vkm_backend_gl
    PUBLIC
        vkm_opengl
        vkm_render
        vkm_loader
    PRIVATE
        glfw               # the window surface: context hints, swap
        vkm_warnings
)

# A shipped game asks the driver for no debug output (gl_window_surface.cpp).
if(VKM_SHIPPING)
    target_compile_definitions(vkm_backend_gl PRIVATE VKM_SHIPPING=1)
endif()

if(NOT VKM_SHIPPING)
    add_subdirectory(editor)
endif()
