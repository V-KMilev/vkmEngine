# Stress Arena - the profiling load, as a project.
#
# Everything here is what a game made with vkmEngine looks like: a project.json
# saying what the game is, gameplay sources compiled into one module the engine
# loads, and the project's own scenes and assets beside them. The engine knows
# none of it by name.
#
# One file, two ways in. `vkm build examples/stress_arena` configures it as a
# project of its own and finds the engine exactly as a game made with `vkm new`
# does; the engine's build adds it as a subdirectory, where those targets are
# already defined and find_package would be asking for what is in the room.
cmake_minimum_required(VERSION 3.25)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(StressArena LANGUAGES CXX)
    # No version pin, unlike the one `vkm new` stamps into a generated project:
    # this example lives in the engine's tree and tracks whatever engine it is
    # built beside, so pinning could only ever be wrong.
    find_package(vkmEngine REQUIRED)
endif()

file(GLOB_RECURSE STRESS_ARENA_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

# The SDK's own recipe: one shared library named `game` in this project's bin/,
# linked against the engine rather than against a host, so the editor and the
# runtime load the same file.
vkm_add_gameplay_module(stress_arena_module SOURCES ${STRESS_ARENA_SOURCES})

target_include_directories(stress_arena_module PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# The engine's own warning flags. A generated project does not get these - they
# are how the engine holds itself, not something the SDK imposes - but the
# examples are ours and are held to it.
target_link_libraries(stress_arena_module PRIVATE vkmEngine::vkm_warnings)
