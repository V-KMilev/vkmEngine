# Stress Arena - the profiling load, as a project.
#
# One file, two ways in: `vkm build examples/stress_arena` configures it
# standalone and finds the engine the way a project from `vkm new` does, and the
# engine's own build adds it as a subdirectory, where those targets already
# exist. Both paths in docs/reference/building.md.
cmake_minimum_required(VERSION 3.25)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(StressArena LANGUAGES CXX)
    find_package(vkmEngine REQUIRED)
endif()

# Compiles src/ into the module a host loads; where it lands, and why, is
# cmake/gameplay_module.cmake.
vkm_add_gameplay_module(stress_arena_module)

# The engine's own warning flags. A generated project does not get these - they
# are how the engine holds itself, not something the SDK imposes - but the
# examples are ours and are held to it.
target_link_libraries(stress_arena_module PRIVATE vkmEngine::vkm_warnings)
