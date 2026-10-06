cmake_minimum_required(VERSION 3.25.0)

set(PROJECT_NAME vkmEngine)
set(PROJECT_VERSION_MAJOR 1)
set(PROJECT_VERSION_MINOR 0)
set(PROJECT_VERSION_PATCH 0)

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
# outright on a thread-local relocation.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# The shared engine sits beside the exes, so the build tree needs no rpath
# tricks to run in place.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Binaries find their libraries beside themselves, in the build tree too:
# `vkm package` copies them out, and an absolute path would hold only here.
set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
set(CMAKE_INSTALL_RPATH "$ORIGIN")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# An in-source build scatters generated files through the tree and makes a clean
# checkout impossible to tell from a dirty one.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are not supported. Configure into a subdirectory:\n"
        "  cmake -B build -G Ninja"
    )
endif()

# The engine a shipped game runs on, as a build tree of its own: optimised, no
# profiler, debug info kept for crash reports. It builds the runtime and the
# server alone - the examples' modules would land where the development hosts
# load them.
option(VKM_SHIPPING "Build the engine a shipped game runs on" OFF)

# VKM_ASSERT and assert() in every build but the shipping one, whatever its
# NDEBUG (src/engine/CMakeLists.txt). Off for a tree that profiles.
option(VKM_ASSERTS "Keep VKM_ASSERT and assert() on whatever the build type" ON)

if(VKM_SHIPPING)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set(VKM_PROFILER OFF CACHE BOOL "Enable Tracy CPU + GPU profiling" FORCE)
    set(VKM_ASSERTS OFF CACHE BOOL "Keep VKM_ASSERT and assert() on whatever the build type" FORCE)
    if(NOT MSVC)
        add_compile_options(-g)
    endif()
endif()

# Single-config generators leave CMAKE_BUILD_TYPE empty, which silently means
# "no optimisation and no debug info". Name the default instead of inheriting it.
get_property(_vkm_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _vkm_multi_config AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
    message(STATUS "No build type specified - defaulting to RelWithDebInfo")
endif()

# This block is the only thing that turns Tracy off: debug/profiler.h keys on
# VKM_PROFILER alone and knows nothing about the build type.
if(NOT DEFINED VKM_PROFILER)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(VKM_PROFILER OFF CACHE BOOL "Enable Tracy CPU + GPU profiling")
    else()
        set(VKM_PROFILER ON  CACHE BOOL "Enable Tracy CPU + GPU profiling")
    endif()
endif()

# The compiler's runtime libraries (GCC's or LLVM's), copied beside the
# executables and installed with them: without them a program fails with no log
# line. On Windows they sit beside the compiler, or in a cross toolchain's target
# bin/; on Linux GCC names them, and a system compiler's are the system's own.
set(VKM_COMPILER_RUNTIME "")
if(MINGW)
    get_filename_component(_vkm_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(_vkm_runtime_dirs ${_vkm_compiler_bin})
    foreach(_root ${CMAKE_FIND_ROOT_PATH})
        list(APPEND _vkm_runtime_dirs ${_root}/bin)
    endforeach()
    foreach(_dll libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll libc++.dll libunwind.dll)
        foreach(_dir ${_vkm_runtime_dirs})
            if(EXISTS "${_dir}/${_dll}")
                file(COPY "${_dir}/${_dll}" DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
                list(APPEND VKM_COMPILER_RUNTIME "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${_dll}")
                break()
            endif()
        endforeach()
    endforeach()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    foreach(_lib libstdc++.so.6 libgcc_s.so.1)
        execute_process(
            COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=${_lib}
            OUTPUT_VARIABLE _vkm_found
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        file(REAL_PATH "${_vkm_found}" _vkm_found)
        if(EXISTS "${_vkm_found}" AND NOT _vkm_found MATCHES "^/(usr|lib)")
            file(MAKE_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
            # Under the soname the loader asks for, not the versioned file name.
            file(COPY_FILE "${_vkm_found}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${_lib}" ONLY_IF_DIFFERENT)
            list(APPEND VKM_COMPILER_RUNTIME "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${_lib}")
        endif()
    endforeach()
endif()

# Sanitizers for a build tree of their own - `-DVKM_SANITIZE=address,undefined`.
# Global rather than first-party only: a buffer overrun in a submodule, reached
# through the engine's call, is the engine's bug to find. Off by default because
# it roughly doubles the frame time and the memory.
set(VKM_SANITIZE "" CACHE STRING "Comma-separated -fsanitize= list, e.g. address,undefined")
if(VKM_SANITIZE)
    add_compile_options(-fsanitize=${VKM_SANITIZE} -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=${VKM_SANITIZE})
endif()

# Branch / commit / build-date -> PROJECT_* variables, recomputed each configure.
include(cmake/git_metadata.cmake)

# Warning flags shared by every first-party target. GCC/Clang dialect only -
# MSVC spells these differently, so the generator expression leaves it alone.
set(_vkm_gcc_warnings
    -Wall
    -Wextra
    # An interface method that ignores an argument is the point of a default
    # implementation, and the tree is full of them.
    -Wno-unused-parameter

    # Flags the tree already satisfies, kept on so it goes on satisfying them.
    # They are here rather than in CMAKE_CXX_FLAGS because that would apply them
    # to the submodules too, which -Wshadow alone would bury in warnings.
    -Wshadow              # a name that hides an outer one, which is how a lambda
                          # reuses its enclosing loop's counter and still compiles
    -Wunused-variable     # a local nothing reads is code that stopped being run
    -Wunused-value        # an expression computed and thrown away
    -Wformat=2            # includes -Wformat-nonliteral: a printf format chosen at
                          # run time is a label today and undefined behaviour the
                          # day somebody puts a % in one of its branches
    -Wnon-virtual-dtor    # deleting a derived object through a base without one
    -Woverloaded-virtual  # an overload that hides a virtual instead of overriding it
)
add_library(vkm_warnings INTERFACE)
target_compile_options(vkm_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang>:${_vkm_gcc_warnings}>
    # GCC alone has it: Name's fixed-buffer strncpy truncates by design.
    $<$<CXX_COMPILER_ID:GNU>:-Wno-stringop-truncation>
    # Clang reads the doc blocks: a @param that names no parameter, a @return on
    # a function returning nothing. An error, so a Clang build is the check;
    # GCC has no equivalent.
    $<$<CXX_COMPILER_ID:Clang>:-Werror=documentation>
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
if(NOT VKM_SHIPPING)
    add_subdirectory(src/editor)
endif()
add_subdirectory(src/backend/opengl)

# The recipe a gameplay module is built by, and the names it uses. An
# installed SDK hands both to a project through find_package; here they are
# included and aliased directly, so the examples below are written the one way
# rather than one way in this tree and another in a user's.
include(cmake/gameplay_module.cmake)
add_library(vkmEngine::vkm_core     ALIAS vkm_core)
add_library(vkmEngine::vkm_warnings ALIAS vkm_warnings)

if(NOT VKM_SHIPPING)
    # Engine tests. After the libraries they link and the module recipe their test
    # module is built by, before the executables, which they are not part of.
    add_subdirectory(tests)

    # Example projects, added here so a build of the engine keeps them compiling
    # against it. `vkm build examples/<name>` configures the same file standalone,
    # which is the path a user's own project takes.
    add_subdirectory(examples/potion_runner)
    add_subdirectory(examples/stress_arena)
    add_subdirectory(examples/physics_lab)
endif()

# Thin executables, each linking the engine libs itself. What each host is and
# what it links: docs/reference/building.md.
#
# None of them export symbols. A gameplay module links the shared engine itself,
# so it resolves against that rather than against whichever host opened it - which
# is what lets one module file serve any host.
add_subdirectory(app/runtime)
add_subdirectory(app/server)
if(NOT VKM_SHIPPING)
    add_subdirectory(app/cook)
    add_subdirectory(app/editor)
endif()

# The install rules, after the targets they install.
include(cmake/install.cmake)

# CPack, which turns that install tree into a downloadable archive. After the
# install rules because it packages what they produce.
include(cmake/package.cmake)
include(cmake/docs.cmake)
