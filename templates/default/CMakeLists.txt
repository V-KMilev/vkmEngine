# A vkmEngine project.
#
# The engine is found, not built: find_package gives the imported targets and the
# vkm_add_gameplay_module() helper below, and the SDK's toolchain pin fails
# configure here rather than letting a mismatched compiler fail at run time.
#
# You normally never invoke this by hand - `vkm build` does it for you.
cmake_minimum_required(VERSION 3.25)
project(NewProject LANGUAGES CXX)

# @VKM_ENGINE_VERSION@ is filled in by `vkm new` with the version of the SDK the
# project was created from, so the request matches the engine that answered it.
# A placeholder rather than a literal on purpose: a number written here would be
# right on the day it was typed and wrong for every release after, and the
# package is SameMinorVersion-compatible, so wrong means configure refuses.
find_package(vkmEngine @VKM_ENGINE_VERSION@ REQUIRED)

file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

# Compiles src/ into the one module a host loads. The output lands in this
# project's bin/ as `game`, which is the only place the engine looks.
vkm_add_gameplay_module(game_module SOURCES ${GAME_SOURCES})

target_include_directories(game_module PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
