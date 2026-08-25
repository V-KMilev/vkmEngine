# The vkm_core test suite. Covers engine logic that is pure computation over a
# Scene, so the binary runs on a build machine with no GPU and no display. Built
# with the engine, because a suite that is opt-in is a suite nobody runs.
add_executable(vkm_engine_tests ${CMAKE_CURRENT_SOURCE_DIR}/vkm_engine_tests.cpp)

target_link_libraries(vkm_engine_tests PRIVATE
    vkmEngine::vkm_core
    vkmEngine::vkm_warnings
)

add_test(NAME vkm_engine COMMAND vkm_engine_tests)
