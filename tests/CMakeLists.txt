# The vkm_core test suite. Covers engine logic that is pure computation over a
# Scene, so the binary runs on a build machine with no GPU and no display. Built
# with the engine, because a suite that is opt-in is a suite nobody runs.
#
# One file per subsystem, each ending in the one entry point main calls. Globbed
# rather than listed: a new suite should be a new file and a line in suites.h,
# not three edits.
file(GLOB VKM_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(vkm_engine_tests ${VKM_TEST_SOURCES})

target_include_directories(vkm_engine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vkm_engine_tests PRIVATE
    vkmEngine::vkm_core
    vkmEngine::vkm_warnings
)

# Where the shipped example projects are, so a test can open the scene of record
# and check the format still reads it. A packaged SDK has no examples/ - the
# test that uses this says so and passes rather than failing on a missing file.
target_compile_definitions(vkm_engine_tests PRIVATE
    VKM_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples"
)

# No arguments runs every suite, which is what CI wants. Naming one or more
# runs just those, for the person working on it:
#     ./build/bin/vkm_engine_tests physics wire
#     ./build/bin/vkm_engine_tests --list
add_test(NAME vkm_engine COMMAND vkm_engine_tests)
