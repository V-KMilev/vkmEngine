# The vkm_core test suite. Covers engine logic that is pure computation over a
# Scene, so the binary runs on a build machine with no GPU and no display. Built
# with the engine, because a suite that is opt-in is a suite nobody runs.
#
# One file per subsystem, each ending in the one entry point main calls. Globbed
# rather than listed: a new suite should be a new file and a line in suites.h,
# not three edits.
file(GLOB VKM_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

# PlaySnapshot lives in src/editor but depends on nothing from it - a scene, a
# resource manager and the two serializers. Compiled in here so the play/stop
# round-trip can be tested at all: it could not be while it was four fields on an
# editor class that needs a window to exist.
add_executable(vkm_engine_tests
    ${VKM_TEST_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/editor/framework/play_snapshot.cpp
)

target_include_directories(vkm_engine_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/editor
)

target_link_libraries(vkm_engine_tests PRIVATE
    vkmEngine::vkm_core
    vkmEngine::vkm_warnings
)

# The transport suite opens a socket of its own, to send the zero-length
# datagram UdpSocket refuses to emit. vkm_core links ws2_32 PRIVATE - it is an
# implementation detail of its own sockets, not part of what it offers - so a
# caller that calls socket() itself asks for the library itself.
if(WIN32)
    target_link_libraries(vkm_engine_tests PRIVATE ws2_32)
endif()

# Where the shipped example projects are, so a test can open the scene of record
# and check the format still reads it. A packaged SDK has no examples/ - the
# test that uses this says so and passes rather than failing on a missing file.
target_compile_definitions(vkm_engine_tests PRIVATE
    VKM_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples"
    VKM_DOCS_DIR="${CMAKE_SOURCE_DIR}/docs"
)

# No arguments runs every suite, which is what CI wants. Naming one or more
# runs just those, for the person working on it:
#     ./build/bin/vkm_engine_tests physics wire
#     ./build/bin/vkm_engine_tests --list
add_test(NAME vkm_engine COMMAND vkm_engine_tests)

# The GPU suite, built only where EGL can be found.
#
# Separate from vkm_engine_tests on purpose: that binary must stay runnable on a
# machine with no GPU and no graphics libraries at all, and linking EGL into it
# would end that. This one links EGL, opens the device directly, and skips at
# runtime when there is no device to open - so it is green on a build machine and
# useful on a workstation.
find_package(OpenGL QUIET COMPONENTS EGL)
if(TARGET OpenGL::EGL)
    add_executable(vkm_render_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/render/render_tests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/render/egl_context.cpp
    )
    target_include_directories(vkm_render_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/render)

    # vkm_backend_gl as well as vkm_gl: the suite compiles the shipped shaders
    # with the backend's own prelude, which is GLBackend's to build. Nothing here
    # opens a window - the EGL context above is the whole GL surface.
    target_link_libraries(vkm_render_tests PRIVATE
        vkm_backend_gl vkm_gl vkm_log OpenGL::EGL)

    # Where the shaders are. The loader resolves their paths against the working
    # directory, which the hosts pin to the engine root at startup.
    target_compile_definitions(vkm_render_tests PRIVATE
        VKM_ENGINE_ROOT="${CMAKE_SOURCE_DIR}")
    set_target_properties(vkm_render_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    add_test(NAME render COMMAND vkm_render_tests)
    message(STATUS "EGL found - the GPU test suite (vkm_render_tests) is built")
else()
    message(STATUS "EGL not found - the GPU test suite is skipped")
endif()
