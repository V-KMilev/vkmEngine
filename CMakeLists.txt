cmake_minimum_required(VERSION 3.25.0)

set(PROJECT_NAME vkmEngine)
set(PROJECT_VERSION_MAJOR 1)
set(PROJECT_VERSION_MINOR 3)
set(PROJECT_VERSION_PATCH 0)

project(
  ${PROJECT_NAME}
  VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}
  LANGUAGES C CXX
)

# First-party targets build as strict C++17 (no GNU extensions). Submodules set
# their own standard per-target, so this default only governs our code.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Place all binaries (exe + DLLs) in one directory so an exe finds its DLLs.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Profiler (Tracy) toggle. Defaults ON except for explicit Release builds, where
# debug/profiler.h expands every macro to a zero-cost no-op and Tracy compiles
# to nothing.
if(NOT DEFINED VKM_PROFILER)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(VKM_PROFILER OFF CACHE BOOL "Enable Tracy CPU + GPU profiling")
    else()
        set(VKM_PROFILER ON  CACHE BOOL "Enable Tracy CPU + GPU profiling")
    endif()
endif()

# Branch / commit / build-date -> PROJECT_* variables, recomputed each configure.
include(cmake/git_metadata.cmake)

# Derive shaders/_generated/engine_config.glsl from src/engine/core/engine_config.h
# so cross-language constants (MAX_LIGHTS, NUM_CASCADES, ...) have one C++ source
# of truth instead of comment-driven mirroring in each shader.
include(cmake/generate_shader_config.cmake)

# Warning flags shared by every first-party target. GCC/Clang dialect only -
# MSVC uses a different flag spelling, so the genexpr leaves it untouched.
# Targets opt in by linking vkm_warnings; submodules don't, so third-party code
# keeps its own warning level.
set(_vkm_gcc_warnings
    -Wall
    -Wextra
    -Wno-unused-value         # VKM_ASSERT expands to `true` in release
    -Wno-unused-parameter     # interface stubs, placeholders
    -Wno-unused-variable      # system refs kept for readability
    -Wno-stringop-truncation  # Name's fixed-buffer strncpy is intentional
)
add_library(vkm_warnings INTERFACE)
target_compile_options(vkm_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang>:${_vkm_gcc_warnings}>
)

# Compile-time build metadata, baked into the binary through the BuildInfo
# facade (debug/build_info.h). Defined before the subdirectories so each target
# can link it where it is defined.
add_library(BuildInfo INTERFACE)
target_compile_definitions(BuildInfo INTERFACE
    APP_NAME="${PROJECT_NAME}"
    APP_BRANCH="${PROJECT_BRANCH}"
    APP_BUILD_DATE="${PROJECT_BUILD_DATE}"
    APP_ROOT_DIR="${CMAKE_SOURCE_DIR}"

    APP_VERSION="${PROJECT_VERSION}"
    APP_COMMIT_HASH="${PROJECT_COMMIT_HASH}"

    VKMGL_VERSION="${VKMGL_VERSION}"
    VKMGL_COMMIT_HASH="${VKMGL_COMMIT_HASH}"

    VKMLOG_VERSION="${VKMLOG_VERSION}"
    VKMLOG_COMMIT_HASH="${VKMLOG_COMMIT_HASH}"
)

add_subdirectory(modules)

add_subdirectory(src/engine)
add_subdirectory(src/tools)
add_subdirectory(src/editor)
add_subdirectory(src/backend/opengl)
# Example projects. Not part of the engine - each is a project in the shape a
# real one takes, built from this tree because there is no installed SDK to
# build them against yet.
add_subdirectory(examples/potion_runner)
add_subdirectory(examples/stress_arena)

# Three thin executables under app/. engine_runtime plays a project, engine_editor
# adds the in-process editor to edit one, and engine_cook bakes a project's assets
# with no window at all. The first two link the engine libs directly and share the
# header-only bootstrap in app/engine_app.h (setupEngineApp) - the editor's only
# addition is its EditorSystem; the cooker builds no Engine.
add_subdirectory(app/runtime)
add_subdirectory(app/cooker)
add_subdirectory(app/editor)
