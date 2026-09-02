cmake_minimum_required(VERSION 3.25.0)

set(PROJECT_NAME vkmEngine)
set(PROJECT_VERSION_MAJOR 0)
set(PROJECT_VERSION_MINOR 8)
set(PROJECT_VERSION_PATCH 1)

project(
  ${PROJECT_NAME}
  VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}
  LANGUAGES C CXX
)

# Submodules set their own standard per target, so this default governs
# first-party code only.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Every target is position-independent. The engine libraries are shared, and a
# static dependency linked into a shared library has to be PIC or the link fails
# outright on a thread-local relocation (stb was the one that found this).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# The shared engine sits beside the exes, so the build tree needs no rpath
# tricks to run in place.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Binaries find their libraries relative to themselves rather than through a
# path baked at build time: that is what makes an installed SDK relocatable, and
# what lets `vkm package` copy binaries out of a build tree without shipping a
# game that only starts on the machine that built it. CMake still adds the
# link-path entries to the build rpath, so running out of build/bin is
# unaffected.
set(CMAKE_SKIP_BUILD_RPATH OFF)
set(CMAKE_BUILD_WITH_INSTALL_RPATH OFF)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)
if(APPLE)
    set(CMAKE_BUILD_RPATH   "@loader_path")
    set(CMAKE_INSTALL_RPATH "@loader_path;@loader_path/../lib")
else()
    set(CMAKE_BUILD_RPATH   "$ORIGIN")
    set(CMAKE_INSTALL_RPATH "$ORIGIN;$ORIGIN/../lib")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# An in-source build scatters generated files through the tree and makes a clean
# checkout impossible to tell from a dirty one.
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

# Tracy is off in an explicit Release build: debug/profiler.h expands every
# macro to a zero-cost no-op there, so the client compiles to nothing anyway.
if(NOT DEFINED VKM_PROFILER)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(VKM_PROFILER OFF CACHE BOOL "Enable Tracy CPU + GPU profiling")
    else()
        set(VKM_PROFILER ON  CACHE BOOL "Enable Tracy CPU + GPU profiling")
    endif()
endif()

# The compiler's runtime DLLs, in the BUILD tree and not only at install time:
# the engine's shared libraries link libstdc++ dynamically - only targets that
# link assimp inherit its -static-libstdc++ - so running straight out of
# build/bin without these fails with a system dialog and no log line.
if(MINGW)
    get_filename_component(_vkm_mingw_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(_dll libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
        if(EXISTS "${_vkm_mingw_bin}/${_dll}")
            file(COPY "${_vkm_mingw_bin}/${_dll}"
                 DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
        endif()
    endforeach()
endif()

# Branch / commit / build-date -> PROJECT_* variables, recomputed each configure.
include(cmake/git_metadata.cmake)

# Derives shaders/_generated/ from the C++ headers, so a cross-language constant
# has one source of truth instead of a hand-kept copy in each shader.
include(cmake/generate_shader_config.cmake)

# Warning flags shared by every first-party target. GCC/Clang dialect only -
# MSVC spells these differently, so the generator expression leaves it alone.
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

# Compile-time build metadata, baked into the binary through the facade in
# debug/build_info.h. Defined before the subdirectories so each target can link
# it where it is defined.
add_library(vkm_build_info INTERFACE)
target_compile_definitions(vkm_build_info INTERFACE
    APP_NAME="${PROJECT_NAME}"
    APP_BRANCH="${PROJECT_BRANCH}"
    APP_BUILD_DATE="${PROJECT_BUILD_DATE}"
    APP_ROOT_DIR="${CMAKE_SOURCE_DIR}"

    APP_VERSION="${PROJECT_VERSION}"
    APP_COMMIT_HASH="${PROJECT_COMMIT_HASH}"

    VKM_GL_VERSION="${VKM_GL_VERSION}"
    VKM_GL_COMMIT_HASH="${VKM_GL_COMMIT_HASH}"

    VKM_LOG_VERSION="${VKM_LOG_VERSION}"
    VKM_LOG_COMMIT_HASH="${VKM_LOG_COMMIT_HASH}"
)

# Testing has to be enabled in the *top-level* directory or CMake writes no test
# file beside it, and `ctest` in build/ reports zero tests however many
# `add_test` calls the subdirectories make.
enable_testing()

add_subdirectory(modules)

add_subdirectory(src/engine)
add_subdirectory(src/tools)
add_subdirectory(src/editor)
add_subdirectory(src/backend/opengl)

# Engine tests. After the libraries they link, before the executables, which
# they are not part of.
add_subdirectory(tests)
# The recipe every gameplay module is built by, and the names it uses. An
# installed SDK hands both to a project through find_package; here they are
# included and aliased directly, so the examples below are written the one way
# rather than one way in this tree and another in a user's.
include(cmake/gameplay_module.cmake)
add_library(vkmEngine::vkm_core     ALIAS vkm_core)
add_library(vkmEngine::vkm_warnings ALIAS vkm_warnings)

# Example projects, added here so a build of the engine keeps them compiling
# against it. `vkm build examples/<name>` configures the same file standalone,
# which is the path a user's own project takes.
add_subdirectory(examples/potion_runner)
add_subdirectory(examples/stress_arena)
add_subdirectory(examples/physics_lab)

# Three thin executables, each linking the engine libs itself and putting the
# repo root on its include path so the "app/" prefix resolves. What each host is
# and what it links: docs/reference/building.md.
#
# None of them export symbols. A gameplay module links the shared engine itself,
# so it resolves against that rather than against whichever host opened it - which
# is what lets one module file serve both the editor and the runtime.
add_subdirectory(app/runtime)
add_subdirectory(app/cooker)
add_subdirectory(app/server)
add_subdirectory(app/editor)

# Every install rule lives in one file, after the targets it installs.
include(cmake/install.cmake)

# CPack, which turns that install tree into a downloadable archive. After the
# install rules because it packages what they produce.
include(cmake/package.cmake)
