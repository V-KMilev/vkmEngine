cmake_minimum_required(VERSION 3.25.0)

project(engine
    VERSION 0.0.1
    LANGUAGES C CXX
)

set(PROJECT_ROOT_DIR ${CMAKE_SOURCE_DIR})

# Git metadata
include(cmake/git_metadata.cmake)

# Dependencies
if(NOT TARGET vkmLog)
    add_subdirectory(modules/vkmLog)
endif()

if(NOT TARGET vkmGL)
    add_subdirectory(modules/vkmGL)
endif()

# ============================================================================
# ImGui Library
# ============================================================================
add_library(imgui STATIC
    modules/imgui/imgui.cpp
    modules/imgui/imgui_draw.cpp
    modules/imgui/imgui_tables.cpp
    modules/imgui/imgui_widgets.cpp
    modules/imgui/imgui_demo.cpp
    modules/imgui/backends/imgui_impl_glfw.cpp
    modules/imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC
    modules/imgui
    modules/imgui/backends
)

target_link_libraries(imgui PUBLIC
    glfw
)

# imgui uses its own built-in GL loader (imgui_impl_opengl3_loader.h)
# -- no GLEW dependency, no loader defines needed

# ============================================================================
# Engine Core Library
# NOTE: Re-run cmake after adding/removing source files (GLOB_RECURSE limitation)
# ============================================================================
file(GLOB_RECURSE ENGINECORE_SOURCES
    src/engine/core/*.cpp
    src/engine/ecs/*.cpp
    src/engine/resource/*.cpp
    src/engine/animation/*.cpp
    src/engine/visibility/*.cpp
    src/engine/event/*.cpp
    src/engine/platform/window/*.cpp
    src/engine/platform/threading/*.cpp
    src/engine/debug/*.cpp
)

add_library(EngineCore STATIC ${ENGINECORE_SOURCES})

target_include_directories(EngineCore PUBLIC
    src/engine
)

target_link_libraries(EngineCore PUBLIC
    vkmGL
    vkmLog
)

target_compile_definitions(EngineCore PUBLIC
    ENABLE_STATISTICS_TRACKING=1
    GLM_ENABLE_EXPERIMENTAL
    GLM_FORCE_INTRINSICS
)

# ============================================================================
# Engine Rendering Library
# ============================================================================
file(GLOB_RECURSE ENGINERENDERING_SOURCES src/engine/render/*.cpp)

add_library(EngineRendering STATIC ${ENGINERENDERING_SOURCES})

target_include_directories(EngineRendering PUBLIC
    src/engine
)

target_link_libraries(EngineRendering PUBLIC
    EngineCore
)

# ============================================================================
# OpenGL Backend Library
# ============================================================================
file(GLOB_RECURSE BACKENDOPENGL_SOURCES src/backend/opengl/*.cpp)

add_library(BackendOpenGL STATIC ${BACKENDOPENGL_SOURCES})

target_include_directories(BackendOpenGL PUBLIC
    src/backend/opengl
)

target_link_libraries(BackendOpenGL PUBLIC
    vkmGL
    EngineRendering
    EngineTools
)

# ============================================================================
# Engine Tools Library (Optional utilities)
# ============================================================================
file(GLOB_RECURSE ENGINETOOLS_SOURCES src/tools/*.cpp)

add_library(EngineTools STATIC ${ENGINETOOLS_SOURCES})

target_include_directories(EngineTools PUBLIC
    src/tools
)

target_link_libraries(EngineTools PUBLIC
    EngineCore
)

# ============================================================================
# Engine Editor Library
# ============================================================================
file(GLOB_RECURSE ENGINEEDITOR_SOURCES src/editor/*.cpp)
# SystemMetrics uses GL for VRAM queries - compiled here, not in EngineCore
list(APPEND ENGINEEDITOR_SOURCES src/engine/platform/system_metrics.cpp)

add_library(EngineEditor STATIC ${ENGINEEDITOR_SOURCES})

target_include_directories(EngineEditor PUBLIC
    src/editor
)

target_link_libraries(EngineEditor PUBLIC
    EngineCore
    EngineTools
    imgui
    vkmGL
)

# Shared build info definitions
add_library(BuildInfo INTERFACE)
target_compile_definitions(BuildInfo INTERFACE
    APP_VERSION="${PROJECT_VERSION}"
    APP_NAME="${PROJECT_NAME}"
    APP_BRANCH="${PROJECT_BRANCH}"
    APP_COMMIT_HASH="${PROJECT_COMMIT_HASH}"
    APP_BUILD_DATE="${PROJECT_BUILD_DATE}"
)

target_link_libraries(EngineEditor PRIVATE BuildInfo)

# ============================================================================
# Main Executable
# ============================================================================
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

# Warning flags and C++ standard for all project targets
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
