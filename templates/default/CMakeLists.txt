# A vkmEngine project.
#
# The engine is found, not built: find_package gives the imported targets and the
# vkm_add_gameplay_module() helper below, and the SDK's toolchain pin fails
# configure here rather than letting a mismatched compiler fail at run time.
#
# You normally never invoke this by hand - `vkm build` does it for you.
cmake_minimum_required(VERSION 3.25)
project(NewProject LANGUAGES CXX)

# The version below is the SDK this project was created from, and the package is
# SameMinorVersion-compatible: move to a later minor release and configure stops
# here rather than building your module against an engine it was not written
# against. Raise it when you mean to move.
find_package(vkmEngine @VKM_ENGINE_VERSION@ REQUIRED)

file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

# Compiles src/ into the one module a host loads. The output lands in this
# project's bin/ as `game`, which is the only place the engine looks.
vkm_add_gameplay_module(game_module SOURCES ${GAME_SOURCES})

target_include_directories(game_module PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
