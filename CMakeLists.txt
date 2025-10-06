cmake_minimum_required(VERSION 3.15)

set(VERSION 0.0.1)

project(engine VERSION ${VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(engine
  main.cpp
)

# Derive build metadata if not provided
# Allow overrides via -DPROJECT_BRANCH=..., -DPROJECT_COMMIT_HASH=..., -DPROJECT_BUILD_DATE=...
if (NOT DEFINED PROJECT_BUILD_DATE)
  string(TIMESTAMP PROJECT_BUILD_DATE "%Y-%m-%d %H:%M:%S %Z" UTC)
endif()

if (NOT DEFINED PROJECT_BRANCH OR NOT DEFINED PROJECT_COMMIT_HASH)
  find_package(Git QUIET)
  if (GIT_FOUND)
    if (NOT DEFINED PROJECT_BRANCH)
      execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE PROJECT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _git_branch_result
      )
    endif()
    if (NOT DEFINED PROJECT_COMMIT_HASH)
      execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short=8 HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE PROJECT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _git_hash_result
      )
    endif()
  endif()
endif()

if (NOT DEFINED PROJECT_BRANCH OR PROJECT_BRANCH STREQUAL "")
  set(PROJECT_BRANCH "unknown")
endif()
if (NOT DEFINED PROJECT_COMMIT_HASH OR PROJECT_COMMIT_HASH STREQUAL "")
  set(PROJECT_COMMIT_HASH "unknown")
endif()

target_compile_definitions(engine PRIVATE APP_VERSION=\"${PROJECT_VERSION}\")
target_compile_definitions(engine PRIVATE APP_NAME=\"${PROJECT_NAME}\")
target_compile_definitions(engine PRIVATE APP_VERSION_MAJOR=\"${PROJECT_VERSION_MAJOR}\")
target_compile_definitions(engine PRIVATE APP_VERSION_MINOR=\"${PROJECT_VERSION_MINOR}\")
target_compile_definitions(engine PRIVATE APP_VERSION_PATCH=\"${PROJECT_VERSION_PATCH}\")
target_compile_definitions(engine PRIVATE APP_BRANCH=\"${PROJECT_BRANCH}\")
target_compile_definitions(engine PRIVATE APP_COMMIT_HASH=\"${PROJECT_COMMIT_HASH}\")
target_compile_definitions(engine PRIVATE APP_BUILD_DATE=\"${PROJECT_BUILD_DATE}\")

if (MSVC)
  target_compile_options(engine PRIVATE /W4 /permissive-)
else()
  target_compile_options(engine PRIVATE -Wall -Wextra -Wpedantic)
endif()