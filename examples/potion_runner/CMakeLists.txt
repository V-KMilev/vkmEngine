# Potion Runner - a project, not part of the engine.
#
# Everything here is what a game made with vkmEngine looks like: a project.json
# saying what the game is, gameplay sources compiled into one module the engine
# loads, and the project's own scenes and assets beside them. The engine knows
# none of it by name.
#
# Built from the engine's tree against the engine libraries, exactly as an
# installed SDK will be linked against once find_package(vkmEngine) exists.
file(GLOB_RECURSE POTION_RUNNER_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(potion_runner_module SHARED ${POTION_RUNNER_SOURCES})

target_include_directories(potion_runner_module PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# Links the engine itself, not a host executable. EngineCore is shared, so this
# is a reference to the one copy rather than a second one, and the module is not
# bound to whichever exe loads it - the editor and the runtime both load this
# same file.
target_link_libraries(potion_runner_module PRIVATE EngineCore vkm_warnings)

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
