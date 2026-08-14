# Stress Arena - the profiling load, as a project.
#
# Everything here is what a game made with vkmEngine looks like: a project.json
# saying what the game is, gameplay sources compiled into one module the engine
# loads, and the project's own scenes and assets beside them. The engine knows
# none of it by name.
#
# Built from the engine's tree against EngineHeaders, exactly as the editor's
# module is, because there is no installed SDK to build against yet. This file
# is the only part that changes when there is.
file(GLOB_RECURSE STRESS_ARENA_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(stress_arena_module SHARED ${STRESS_ARENA_SOURCES})

target_include_directories(stress_arena_module PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(stress_arena_module PRIVATE EngineHeaders vkm_warnings)
# Windows needs an import library named at link time, which ties the module to
# one host: it binds to the editor, and engine_runtime cannot load it there.
# Linux has no such link, so both hosts load it.
if(WIN32)
    target_link_libraries(stress_arena_module PRIVATE engine_editor)
endif()

# Named `game` and placed in the project's own bin/, which is where the runtime
# looks first: the module belongs to the project, not to whoever loads it.
set_target_properties(stress_arena_module PROPERTIES
    OUTPUT_NAME game
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin
)
if(WIN32)
    set_target_properties(stress_arena_module PROPERTIES PREFIX "")
endif()
