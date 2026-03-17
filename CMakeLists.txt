cmake_minimum_required(VERSION 3.25.0)

project(engine
    VERSION 0.0.1
    LANGUAGES C CXX
)

set(PROJECT_ROOT_DIR ${CMAKE_SOURCE_DIR})

# Place all binaries (exe + dlls) in a single directory so the exe can find its DLLs
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Git metadata
include(cmake/git_metadata.cmake)

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
add_subdirectory(src/editor)
add_subdirectory(src/backend/opengl)
add_subdirectory(tests)

add_library(BuildInfo INTERFACE)
target_compile_definitions(BuildInfo INTERFACE
    APP_VERSION="${PROJECT_VERSION}"
    APP_NAME="${PROJECT_NAME}"
    APP_BRANCH="${PROJECT_BRANCH}"
    APP_COMMIT_HASH="${PROJECT_COMMIT_HASH}"
    APP_BUILD_DATE="${PROJECT_BUILD_DATE}"
)

target_link_libraries(EngineEditor PRIVATE BuildInfo)

add_executable(${PROJECT_NAME}
    main.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    EngineCore
    EngineRendering
    BackendOpenGL
    EngineTools
    EngineEditor
    BuildInfo
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    APP_VERSION_MAJOR="${PROJECT_VERSION_MAJOR}"
    APP_VERSION_MINOR="${PROJECT_VERSION_MINOR}"
    APP_VERSION_PATCH="${PROJECT_VERSION_PATCH}"
    APP_ROOT_DIR="${PROJECT_ROOT_DIR}"
)

foreach(_target EngineCore EngineRendering EngineTools EngineEditor BackendOpenGL ${PROJECT_NAME})
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

install(TARGETS EngineCore EngineRendering BackendOpenGL EngineTools EngineEditor
                imgui vkmLog vkmGL glm glm-header-only glfw glew BuildInfo
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
install(DIRECTORY src/editor/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vkmEngine/editor
    FILES_MATCHING PATTERN "*.h")
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
