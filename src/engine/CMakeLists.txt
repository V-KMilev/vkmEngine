# ============================================================================
# Engine Core Library
# NOTE: Re-run cmake after adding/removing source files (GLOB_RECURSE limitation)
# ============================================================================
file(GLOB_RECURSE ENGINECORE_SOURCES
    core/*.cpp
    ecs/*.cpp
    resource/*.cpp
    animation/*.cpp
    visibility/*.cpp
    event/*.cpp
    platform/window/*.cpp
    platform/threading/*.cpp
    debug/*.cpp
)

add_library(EngineCore STATIC ${ENGINECORE_SOURCES})

target_include_directories(EngineCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/vkmEngine>
)

# GLEW/GLFW include dirs for window.cpp (glew target uses old-style include_directories)
target_include_directories(EngineCore PRIVATE
    ${PROJECT_SOURCE_DIR}/modules/vkmGL/modules/glew/include
)

target_link_libraries(EngineCore PUBLIC
    glm
    glfw
    vkmLog
)
target_link_libraries(EngineCore PRIVATE
    glew
)

target_compile_definitions(EngineCore PUBLIC
    ENABLE_STATISTICS_TRACKING=1
    GLM_ENABLE_EXPERIMENTAL
    GLM_FORCE_INTRINSICS
)

# ============================================================================
# Engine Rendering Library
# ============================================================================
file(GLOB_RECURSE ENGINERENDERING_SOURCES render/*.cpp)

add_library(EngineRendering STATIC ${ENGINERENDERING_SOURCES})

target_include_directories(EngineRendering PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/vkmEngine>
)

target_link_libraries(EngineRendering PUBLIC
    EngineCore
)
