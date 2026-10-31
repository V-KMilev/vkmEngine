# The GPU tests through the OpenGL backend: the same harness, the engine drawing through GLBackend into
# an off-screen surface the EGL device gives (gpu.h). Built only where EGL is; with no device each test
# is skipped (exit 77), or fails when VKM_GPU_REQUIRED=1. Every test is labeled gpu as well.
find_package(OpenGL QUIET COMPONENTS EGL)
if(TARGET OpenGL::EGL)
    file(GLOB VKM_RENDER_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
    add_executable(vkm_render_tests
        ${VKM_TESTS_DIR}/harness/main.cpp
        ${VKM_TESTS_DIR}/harness/world.cpp
        ${VKM_RENDER_TEST_SOURCES}
    )
    # The pipeline for its lightmap unwrap: a fixed room's meshes are cooked with one, as a project's are.
    target_link_libraries(vkm_render_tests PRIVATE
        vkm_backend_gl
        vkm_pipeline
        vkm_loader
        vkm_render
        vkmEngine::vkm_core
        stb
        OpenGL::EGL
        vkmEngine::vkm_warnings
    )
    vkm_test_binary(vkm_render_tests gpu)
endif()
