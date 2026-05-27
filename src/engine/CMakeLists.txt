# ============================================================================
# Engine Core Library
# CONFIGURE_DEPENDS lets the build re-glob on demand; adding/removing a
# source file no longer needs a manual cmake reconfigure.
# ============================================================================
file(GLOB_RECURSE ENGINECORE_SOURCES CONFIGURE_DEPENDS
    core/*.cpp
    ecs/*.cpp
    io/*.cpp
    resource/*.cpp
    system/animation/*.cpp
    system/camera/*.cpp
    system/visibility/*.cpp
    system/event/*.cpp
    system/hierarchy/*.cpp
    system/io/*.cpp
    platform/window/*.cpp
    platform/threading/*.cpp
    debug/*.cpp
)

add_library(EngineCore STATIC ${ENGINECORE_SOURCES})

target_include_directories(EngineCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/vkmEngine>
)

# GLEW/GLFW include dirs for window.cpp (glew target uses old-style include_directories).
# stb_image_write for io/screenshot.cpp - that TU defines STB_IMAGE_WRITE_IMPLEMENTATION.
target_include_directories(EngineCore PRIVATE
    ${PROJECT_SOURCE_DIR}/modules/vkmGL/modules/glew/include
    ${PROJECT_SOURCE_DIR}/modules/vkmGL/modules/stb
)

target_link_libraries(EngineCore PUBLIC
    glm
    glfw
    vkmLog
    nlohmann_json
)
target_link_libraries(EngineCore PRIVATE
    glew
)

target_compile_definitions(EngineCore PUBLIC
    GLM_ENABLE_EXPERIMENTAL
    GLM_FORCE_INTRINSICS
)

# Tracy plumbing. The facade (debug/profiler.h) gates every macro on
# VKM_PROFILER, so consumers don't need to include Tracy headers themselves.
# TracyClient stays a PUBLIC link so backend/editor TUs that pull profile
# macros via the facade get the symbols at link time.
if(VKM_PROFILER)
    target_compile_definitions(EngineCore PUBLIC VKM_PROFILER=1)
    target_link_libraries     (EngineCore PUBLIC TracyClient)
endif()

# ============================================================================
# Engine Rendering Library
# ============================================================================
file(GLOB_RECURSE ENGINERENDERING_SOURCES CONFIGURE_DEPENDS system/render/*.cpp)

add_library(EngineRendering STATIC ${ENGINERENDERING_SOURCES})

target_include_directories(EngineRendering PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/vkmEngine>
)

target_link_libraries(EngineRendering PUBLIC
    EngineCore
)
