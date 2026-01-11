cmake_minimum_required(VERSION 3.25.0)

# Define project version
set(PROJECT_NAME engine)
set(PROJECT_VERSION_MAJOR 0)
set(PROJECT_VERSION_MINOR 0)
set(PROJECT_VERSION_PATCH 1)
set(PROJECT_ROOT_DIR ${CMAKE_SOURCE_DIR})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Git metadata
include(cmake/git_metadata.cmake)

project(
  ${PROJECT_NAME}
  VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}
  LANGUAGES C CXX
)

# Dependencies
if(NOT TARGET vkmLog)
    add_subdirectory(modules/vkmLog)
endif()

if(NOT TARGET vkmGL)
    add_subdirectory(modules/vkmGL)
endif()

# ============================================================================
# Engine Core Library
# ============================================================================
add_library(EngineCore STATIC
    # ECS
    src/engine/ecs/scene.h
    src/engine/ecs/entity.h
    src/engine/ecs/component_storage.h
    src/engine/ecs/components/transform.h
    src/engine/ecs/components/camera.h
    src/engine/ecs/components/mesh.h
    src/engine/ecs/components/animation.h
    # Resources
    src/engine/resources/resource_manager.h
    src/engine/resources/storage.h
    src/engine/resources/resource.h
    src/engine/resources/resource_handle.h
    src/engine/resources/assets/mesh_asset.h
    src/engine/resources/assets/mesh_asset.cpp
    src/engine/resources/assets/material_asset.h
    src/engine/resources/assets/texture_asset.h
    # Animation
    src/engine/animation/animation_manager.h
    src/engine/animation/animation_manager.cpp
    src/engine/animation/animation_track.h
    src/engine/animation/keyframe.h
    src/engine/animation/easing.h
    # Events
    src/engine/events/event_manager.h
    src/engine/events/event_manager.cpp
    src/engine/events/event.h
    src/engine/events/event.cpp
    src/engine/events/event_listener.h
    src/engine/events/event_listener.cpp
    # Platform
    src/engine/platform/window/window_manager.h
    src/engine/platform/window/window_manager.cpp
    src/engine/platform/window/window.h
    src/engine/platform/window/window.cpp
    src/engine/platform/window/input_handle.h
    src/engine/platform/window/input_handle.cpp
    src/engine/platform/window/frame_limiter.h
    src/engine/platform/window/frame_limiter.cpp
    src/engine/platform/window/glfw_include.h
    src/engine/platform/threading/thread_pool.h
    src/engine/platform/threading/thread_pool.cpp
    # Debug
    src/engine/debug/statistics.h
    src/engine/debug/statistics.cpp
    src/engine/debug/frame_info.h
    src/engine/debug/trackers/frame_tracker.h
    src/engine/debug/trackers/frame_tracker.cpp
    src/engine/debug/trackers/call_tracker.h
    src/engine/debug/trackers/call_tracker.cpp
)

target_include_directories(EngineCore PUBLIC
    src/engine/core
    src/engine/ecs
    src/engine/ecs/components
    src/engine/resources
    src/engine/resources/assets
    src/engine/animation
    src/engine/events
    src/engine/platform/window
    src/engine/platform/threading
    src/engine/debug
    src/engine/debug/trackers
)

target_link_libraries(EngineCore PUBLIC
    # TODO: Remove vkmGL as it only has to be linked to the backend
    vkmGL
    vkmLog
)

# ============================================================================
# Engine Rendering Library
# ============================================================================
add_library(EngineRendering STATIC
    src/engine/rendering/render_manager.h
    src/engine/rendering/render_manager.cpp
    src/engine/rendering/render_backend.h
    src/engine/rendering/pipeline/render_pipeline.h
    src/engine/rendering/pipeline/render_pipeline.cpp
    src/engine/rendering/pipeline/render_pass.h
    src/engine/rendering/view/render_view.h
    src/engine/rendering/view/render_view_builder.h
    src/engine/rendering/view/render_view_builder.cpp
    src/engine/rendering/culling/frustum_culler.h
    src/engine/rendering/culling/frustum_culler.cpp
)

target_include_directories(EngineRendering PUBLIC
    src/engine/rendering
    src/engine/rendering/pipeline
    src/engine/rendering/view
    src/engine/rendering/culling
)

target_link_libraries(EngineRendering PUBLIC
    EngineCore
)

# ============================================================================
# OpenGL Backend Library
# ============================================================================
add_library(BackendOpenGL STATIC
    src/backend/opengl/gl_backend.h
    src/backend/opengl/gl_backend.cpp
    src/backend/opengl/gl_mesh.h
    src/backend/opengl/gl_mesh.cpp
    src/backend/opengl/gl_material.h
    src/backend/opengl/gl_material.cpp
    src/backend/opengl/gl_texture.h
    src/backend/opengl/gl_texture.cpp
    src/backend/opengl/gl_view.h
    src/backend/opengl/gl_view.cpp
    src/backend/opengl/passes/gl_forward_pass.h
    src/backend/opengl/passes/gl_forward_pass.cpp
    src/backend/opengl/passes/gl_aabb_debug_pass.h
    src/backend/opengl/passes/gl_aabb_debug_pass.cpp
)

target_include_directories(BackendOpenGL PUBLIC
    src/backend/opengl
    src/backend/opengl/passes
)

target_link_libraries(BackendOpenGL PUBLIC
    vkmGL
    EngineRendering
)

# ============================================================================
# Engine Tools Library (Optional utilities)
# ============================================================================
add_library(EngineTools STATIC
    src/tools/loaders/texture_loaders.h
    src/tools/loaders/texture_loaders.cpp
    src/tools/generators/mesh_generators.h
    src/tools/generators/mesh_generators.cpp
    src/tools/generators/texture_generators.h
    src/tools/generators/texture_generators.cpp
    src/tools/generators/material_generators.h
    src/tools/generators/material_generators.cpp
)

target_include_directories(EngineTools PUBLIC
    src/tools/loaders
    src/tools/generators
)

target_link_libraries(EngineTools PUBLIC
    EngineCore
)

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
)

# target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)

# Build info definitions
target_compile_definitions(${PROJECT_NAME} PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
    APP_NAME="${PROJECT_NAME}"
    APP_VERSION_MAJOR="${PROJECT_VERSION_MAJOR}"
    APP_VERSION_MINOR="${PROJECT_VERSION_MINOR}"
    APP_VERSION_PATCH="${PROJECT_VERSION_PATCH}"
    APP_BRANCH="${PROJECT_BRANCH}"
    APP_COMMIT_HASH="${PROJECT_COMMIT_HASH}"
    APP_BUILD_DATE="${PROJECT_BUILD_DATE}"
    APP_ROOT_DIR="${PROJECT_ROOT_DIR}"
)
