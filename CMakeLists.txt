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

# Main executable
add_executable(${PROJECT_NAME}
    main.cpp

    src/ecs/component.cpp
    src/ecs/entity.cpp

    src/ecs/components/mesh.cpp
    src/ecs/components/camera.cpp
    src/ecs/components/transform.cpp

    src/render/resource/resource_manager.cpp

    src/render/backend/gl/gl_mesh.cpp
    src/render/backend/gl/gl_view.cpp
    src/render/backend/gl/gl_backend.cpp

    src/render/view/render_view_builder.cpp

    src/render/pipeline/pass/gl_forward_pass.cpp
    src/render/pipeline/render_pipeline.cpp

    src/render/render_manager.cpp
    src/render/scene.cpp

    src/thread/thread_pool.cpp

    src/event/event.cpp
    src/event/event_manager.cpp
    src/event/event_listener.cpp

    src/statistics/statistics.cpp
    src/statistics/trackers/frame_tracker.cpp
    src/statistics/trackers/call_tracker.cpp

    src/window/window.cpp
    src/window/frame_limiter.cpp
    src/window/input_handle.cpp
    src/window/window_manager.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    src/ecs
    src/ecs/components
    src/ecs/entities

    src/render
    src/render/view
    src/render/resource
    src/render/backend
    src/render/backend/gl
    src/render/backend/gl/passes
    src/render/backend/cpu
    src/render/backend/optix

    src/render/pipeline
    src/render/pipeline/pass

    src/statistics
    src/statistics/trackers

    src/thread
    src/event
    src/window

    utils
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    vkmLog
    vkmGL
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
