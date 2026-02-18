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
    # Core
    src/engine/core/engine.h
    src/engine/core/engine.cpp
    src/engine/core/system.h
    # ECS
    src/engine/ecs/scene.h
    src/engine/ecs/entity.h
    src/engine/ecs/component/transform.h
    src/engine/ecs/component/camera.h
    src/engine/ecs/component/mesh.h
    src/engine/ecs/component/animation.h
    # Resources
    src/engine/resource/resource_manager.h
    src/engine/resource/resource.h
    src/engine/resource/resource_handle.h
    src/engine/resource/mesh_asset.h
    src/engine/resource/mesh_asset.cpp
    src/engine/resource/material_asset.h
    src/engine/resource/texture_asset.h
    # Animation
    src/engine/animation/animation_system.h
    src/engine/animation/animation_system.cpp
    src/engine/animation/animation_track.h
    src/engine/animation/keyframe.h
    src/engine/animation/easing.h
    # Visibility (culling, tests)
    src/engine/visibility/visibility_system.h
    src/engine/visibility/visibility_system.cpp
    src/engine/visibility/visibility.h
    src/engine/visibility/visibility_context.h
    src/engine/visibility/bounds_utils.h
    src/engine/visibility/culling/frustum_culler.h
    src/engine/visibility/culling/screen_size_culling.h
    src/engine/visibility/culling/distance_culling.h
    src/engine/visibility/culling/occlusion_culler.h
    # Events
    src/engine/event/event_system.h
    src/engine/event/event_system.cpp
    src/engine/event/event.h
    src/engine/event/event.cpp
    src/engine/event/event_listener.h
    src/engine/event/event_listener.cpp
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
    src/engine/debug/print_helper.h
    src/engine/debug/frame_tracker.h
    src/engine/debug/frame_tracker.cpp
    src/engine/debug/call_tracker.h
    src/engine/debug/call_tracker.cpp
    # Editor
    src/engine/editor/camera_controller.h
    src/engine/editor/camera_controller.cpp
)

target_include_directories(EngineCore PUBLIC
    src/engine
)

target_link_libraries(EngineCore PUBLIC
    # TODO: Remove vkmGL as it only has to be linked to the backend
    vkmGL
    vkmLog
)

target_compile_definitions(EngineCore PUBLIC
    ENABLE_STATISTICS_TRACKING=1
)

# ============================================================================
# Engine Rendering Library
# ============================================================================
add_library(EngineRendering STATIC
    src/engine/render/render_backend.h
    src/engine/render/render_system.h
    src/engine/render/render_system.cpp
    src/engine/render/render_view.h
    src/engine/render/render_view.cpp
    src/engine/render/render_pipeline.h
    src/engine/render/render_pipeline.cpp
    src/engine/render/render_pass.h
)

target_link_libraries(EngineRendering PUBLIC
    EngineCore
)

# ============================================================================
# OpenGL Backend Library
# ============================================================================
add_library(BackendOpenGL STATIC
    # Core backend components
    src/backend/opengl/core/gl_backend.h
    src/backend/opengl/core/gl_backend.cpp
    src/backend/opengl/core/gl_view.h
    src/backend/opengl/core/gl_view.cpp
    src/backend/opengl/core/gl_instance_batcher.h
    src/backend/opengl/core/gl_instance_batcher.cpp
    # Resource wrappers
    src/backend/opengl/resource/gl_mesh.h
    src/backend/opengl/resource/gl_mesh.cpp
    src/backend/opengl/resource/gl_instance_buffer.h
    src/backend/opengl/resource/gl_instance_buffer.cpp
    src/backend/opengl/resource/gl_material.h
    src/backend/opengl/resource/gl_material.cpp
    src/backend/opengl/resource/gl_texture.h
    src/backend/opengl/resource/gl_texture.cpp
    src/backend/opengl/resource/gl_lights.h
    src/backend/opengl/resource/gl_lights.cpp
    # Configuration
    src/backend/opengl/config/gl_config.h
    src/backend/opengl/config/gl_texture_mapping.h
    # Render passes
    src/backend/opengl/pass/gl_forward_pass.h
    src/backend/opengl/pass/gl_forward_pass.cpp
    src/backend/opengl/pass/gl_aabb_debug_pass.h
    src/backend/opengl/pass/gl_aabb_debug_pass.cpp
    src/backend/opengl/pass/gl_grid_pass.h
    src/backend/opengl/pass/gl_grid_pass.cpp
    src/backend/opengl/pass/gl_navigation_gizmo_pass.h
    src/backend/opengl/pass/gl_navigation_gizmo_pass.cpp
    # Convenience header
    src/backend/opengl/gl_common.h
)

target_include_directories(BackendOpenGL PUBLIC
    src/backend/opengl
    src/backend/opengl/core
    src/backend/opengl/resource
    src/backend/opengl/config
    src/backend/opengl/pass
)

target_link_libraries(BackendOpenGL PUBLIC
    vkmGL
    EngineRendering
    EngineTools
)

# ============================================================================
# Engine Tools Library (Optional utilities)
# ============================================================================
add_library(EngineTools STATIC
    src/tools/loader/texture_loaders.h
    src/tools/loader/texture_loaders.cpp
    src/tools/loader/material_loaders.h
    src/tools/loader/material_loaders.cpp

    src/tools/generator/mesh_generators.h
    src/tools/generator/mesh_generators.cpp
    src/tools/generator/texture_generators.h
    src/tools/generator/texture_generators.cpp
    src/tools/generator/material_generators.h
    src/tools/generator/material_generators.cpp
    src/tools/generator/light_generators.h
    src/tools/generator/light_generators.cpp
)

target_include_directories(EngineTools PUBLIC
    src/tools
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
