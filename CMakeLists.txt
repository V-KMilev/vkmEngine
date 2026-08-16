cmake_minimum_required(VERSION 3.25.0)

set(PROJECT_NAME vkmEngine)
set(PROJECT_VERSION_MAJOR 1)
set(PROJECT_VERSION_MINOR 3)
set(PROJECT_VERSION_PATCH 3)

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

# Every target is position-independent. The engine libraries are shared, and a
# static dependency linked into a shared library has to be PIC or the link fails
# outright on a thread-local relocation (stb was the one that found this).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# One place for executables, one for libraries. Keeping the shared engine beside
# the exes means the build tree needs no rpath tricks to run in place.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# An installed SDK is relocatable: binaries find their libraries relative to
# themselves rather than through an absolute path baked at build time. The build
# tree keeps CMake's own generated rpath, which is why this is not applied there.
set(CMAKE_SKIP_BUILD_RPATH OFF)
set(CMAKE_BUILD_WITH_INSTALL_RPATH OFF)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)
if(APPLE)
    set(CMAKE_INSTALL_RPATH "@loader_path;@loader_path/../lib")
else()
    set(CMAKE_INSTALL_RPATH "$ORIGIN;$ORIGIN/../lib")
endif()

# Written to the build dir for clangd, IDEs, and any tool that wants to know how
# a translation unit is actually compiled.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# An in-source build scatters generated files through the tree and makes a clean
# checkout impossible to tell from a dirty one. Refuse rather than warn.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are not supported. Configure into a subdirectory:\n"
        "  cmake -B build -G Ninja")
endif()

# Single-config generators leave CMAKE_BUILD_TYPE empty, which silently means
# "no optimisation and no debug info". Name the default instead of inheriting it.
get_property(_vkm_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _vkm_multi_config AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
    message(STATUS "No build type specified - defaulting to RelWithDebInfo")
endif()

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
