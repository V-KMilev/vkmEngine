cmake_minimum_required(VERSION 3.25.0)

project(engine
    VERSION 0.0.1
    LANGUAGES C CXX
)

set(PROJECT_ROOT_DIR ${CMAKE_SOURCE_DIR})

# Place all binaries (exe + dlls) in a single directory so the exe can find its DLLs
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Profiler (Tracy) toggle. Defaults ON for everything except explicit
# Release builds. When OFF, debug/profiler.h expands every macro to a
# zero-cost no-op and Tracy compiles to nothing.
if(NOT DEFINED VKM_PROFILER)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(VKM_PROFILER OFF CACHE BOOL "Enable Tracy CPU + GPU profiling")
    else()
        set(VKM_PROFILER ON  CACHE BOOL "Enable Tracy CPU + GPU profiling")
    endif()
endif()

# Editor toggle. ON (default) = build both engine_editor and engine_runtime
# executables. OFF = build only engine_runtime, and src/editor/ is skipped
# entirely - proves the engine has no hidden dependency on editor code and
# matches the layout a shipped game binary would use.
option(VKM_WITH_EDITOR "Build the engine_editor executable + EngineEditor library" ON)

# Git metadata
include(cmake/git_metadata.cmake)

# Derive shaders/_generated/engine_config.glsl from src/engine/core/engine_config.h
# so the cross-language constants (MAX_LIGHTS, NUM_CASCADES, ...) have a single
# C++ source of truth instead of comment-driven mirroring in each shader file.
include(cmake/generate_shader_config.cmake)

add_subdirectory(modules)

# Fix submodule include dirs for export compatibility (they lack generator expressions)
set_property(TARGET vkmLog PROPERTY INTERFACE_INCLUDE_DIRECTORIES
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/modules/vkmLog/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/vkmLog>
)
set_property(TARGET vkmGL PROPERTY INTERFACE_INCLUDE_DIRECTORIES
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/modules/vkmGL/src>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/modules/vkmGL/src/buffer>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/modules/vkmGL/src/buffer/memory>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/modules/vkmGL/src/buffer/state>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/modules/vkmGL/src/core>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/modules/vkmGL/src/shader>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/modules/vkmGL/src/texture>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/modules/vkmGL/src/components>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/modules/vkmGL/utils>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/vkmGL>
)
set_property(TARGET glew PROPERTY INTERFACE_INCLUDE_DIRECTORIES
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/modules/vkmGL/modules/glew/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

add_subdirectory(src/engine)
add_subdirectory(src/tools)
if(VKM_WITH_EDITOR)
    add_subdirectory(src/editor)
endif()
add_subdirectory(src/backend/opengl)
add_subdirectory(src/engine_app)

add_library(BuildInfo INTERFACE)
target_compile_definitions(BuildInfo INTERFACE
    APP_VERSION="${PROJECT_VERSION}"
    APP_VERSION_MAJOR="${PROJECT_VERSION_MAJOR}"
    APP_VERSION_MINOR="${PROJECT_VERSION_MINOR}"
    APP_VERSION_PATCH="${PROJECT_VERSION_PATCH}"
    APP_NAME="${PROJECT_NAME}"
    APP_BRANCH="${PROJECT_BRANCH}"
    APP_COMMIT_HASH="${PROJECT_COMMIT_HASH}"
    APP_BUILD_DATE="${PROJECT_BUILD_DATE}"
    APP_ROOT_DIR="${PROJECT_ROOT_DIR}"
)

if(VKM_WITH_EDITOR)
    target_link_libraries(EngineEditor PRIVATE BuildInfo)
endif()

# EngineCore's io/screenshot.cpp writes into APP_ROOT_DIR/screenshots.
target_link_libraries(EngineCore PRIVATE BuildInfo)

# EngineTools resolves relative asset paths (HDR env / color LUT) against
# APP_ROOT_DIR, so it needs the same build-info definitions.
target_link_libraries(EngineTools PRIVATE BuildInfo)

# Two thin executable targets, each defined under app/. engine_runtime is
# the bare engine (no editor at all). engine_editor adds EngineEditor and
# instantiates EditorSystem on top of the shared bootstrap. Both pull the
# rest of the engine in through EngineApp.
add_subdirectory(app/engine_runtime)
if(VKM_WITH_EDITOR)
    add_subdirectory(app/engine_editor)
endif()

set(_vkm_engine_targets EngineCore EngineRendering EngineTools BackendOpenGL EngineApp engine_runtime)
if(VKM_WITH_EDITOR)
    list(APPEND _vkm_engine_targets EngineEditor engine_editor)
endif()
foreach(_target ${_vkm_engine_targets})
    set_target_properties(${_target} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_compile_options(${_target} PRIVATE
        -Wall -Wextra
        -Wno-unused-value            # VKM_ASSERT expands to `true` in release
        -Wno-unused-parameter        # Interface stubs, placeholders
        -Wno-unused-variable         # System refs kept for readability
        -Wno-stringop-truncation     # Name component strncpy is intentional
    )
endforeach()

include(GNUInstallDirs)

set(_vkm_install_targets EngineCore EngineRendering BackendOpenGL EngineTools EngineApp
                          imgui vkmLog vkmGL glm glm-header-only glfw glew BuildInfo nlohmann_json)
if(VKM_WITH_EDITOR)
    list(APPEND _vkm_install_targets EngineEditor)
endif()
install(TARGETS ${_vkm_install_targets}
    EXPORT vkmEngineTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install headers
install(DIRECTORY src/engine/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vkmEngine
    FILES_MATCHING PATTERN "*.h")
install(DIRECTORY src/backend/opengl/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vkmEngine/backend/opengl
    FILES_MATCHING PATTERN "*.h")
install(DIRECTORY src/tools/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vkmEngine/tools
    FILES_MATCHING PATTERN "*.h")
install(DIRECTORY src/engine_app/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vkmEngine/engine_app
    FILES_MATCHING PATTERN "*.h")
if(VKM_WITH_EDITOR)
    install(DIRECTORY src/editor/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vkmEngine/editor
        FILES_MATCHING PATTERN "*.h")
endif()
install(DIRECTORY modules/vkmLog/src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vkmLog
    FILES_MATCHING PATTERN "*.h")
install(DIRECTORY modules/vkmGL/src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vkmGL
    FILES_MATCHING PATTERN "*.h")
install(DIRECTORY modules/vkmGL/utils/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vkmGL
    FILES_MATCHING PATTERN "*.h")
install(DIRECTORY modules/imgui/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/imgui
    FILES_MATCHING PATTERN "*.h")
install(DIRECTORY modules/vkmGL/modules/glew/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h")

# Export config
install(EXPORT vkmEngineTargets
    FILE vkmEngineTargets.cmake
    NAMESPACE vkmEngine::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vkmEngine
)

# Package config file
include(CMakePackageConfigHelpers)
configure_package_config_file(
    cmake/vkmEngineConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/vkmEngineConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vkmEngine
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/vkmEngineConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/vkmEngineConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/vkmEngineConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vkmEngine
)
