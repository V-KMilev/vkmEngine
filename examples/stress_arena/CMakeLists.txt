# Stress Arena - the profiling load, as a project.
#
# One file, two ways in: `vkm build examples/stress_arena` configures it
# standalone and finds the engine the way a project from `vkm new` does, and the
# engine's own build adds it as a subdirectory, where those targets already
# exist. Both paths in docs/reference/building.md.
cmake_minimum_required(VERSION 3.25)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(StressArena LANGUAGES CXX)
    # No version pin, unlike the one `vkm new` stamps into a generated project:
    # this example lives in the engine's tree and tracks whatever engine it is
    # built beside, so pinning could only ever be wrong.
    find_package(vkmEngine REQUIRED)
endif()

file(GLOB_RECURSE STRESS_ARENA_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

# Compiles src/ into the one module a host loads, into this project's bin/ as
# `game` - the only place a host looks.
vkm_add_gameplay_module(stress_arena_module SOURCES ${STRESS_ARENA_SOURCES})

target_include_directories(stress_arena_module PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# The engine's own warning flags. A generated project does not get these - they
# are how the engine holds itself, not something the SDK imposes - but the
# examples are ours and are held to it.
target_link_libraries(stress_arena_module PRIVATE vkmEngine::vkm_warnings)
