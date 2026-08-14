# Potion Runner - a project, not part of the engine.
#
# Everything here is what a game made with vkmEngine looks like: a project.json
# saying what the game is, gameplay sources compiled into one module the engine
# loads, and the project's own scenes and assets beside them. The engine knows
# none of it by name.
#
# Built from the engine's tree for now, against EngineHeaders, exactly as the
# editor's module is. From 1.4 this becomes find_package(vkmEngine) and the
# project builds with no engine checkout at all - the CMake here is the only
# part that changes.
file(GLOB_RECURSE POTION_RUNNER_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(potion_runner_module SHARED ${POTION_RUNNER_SOURCES})

target_include_directories(potion_runner_module PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(potion_runner_module PRIVATE EngineHeaders vkm_warnings)
if(WIN32)
    target_link_libraries(potion_runner_module PRIVATE engine_editor)
endif()

# Named `game` and placed in the project's own bin/, which is where the runtime
# looks first: the module belongs to the project, not to whoever loads it.
set_target_properties(potion_runner_module PROPERTIES
    OUTPUT_NAME game
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin
)
if(WIN32)
    set_target_properties(potion_runner_module PROPERTIES PREFIX "")
endif()
