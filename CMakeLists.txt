cmake_minimum_required(VERSION 3.15)

# Define project version
set(PROJECT_NAME engine)
set(PROJECT_VERSION_MAJOR 0)
set(PROJECT_VERSION_MINOR 0)
set(PROJECT_VERSION_PATCH 1)
set(PROJECT_ROOT_DIR ${CMAKE_SOURCE_DIR})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Define Git metadata
include(cmake/git_metadata.cmake)

# Define project
project(${PROJECT_NAME} VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH} LANGUAGES CXX)

set(SOURCES
  modules/vkmLog/logger.cpp

)

set(HEADERS
  modules/vkmLog/logger.h
  utils/build_info.h
)

add_executable(${PROJECT_NAME}
  ${SOURCES}
  ${HEADERS}

  main.cpp
)

target_compile_definitions(${PROJECT_NAME} PRIVATE APP_VERSION=\"${PROJECT_VERSION}\")
target_compile_definitions(${PROJECT_NAME} PRIVATE APP_NAME=\"${PROJECT_NAME}\")
target_compile_definitions(${PROJECT_NAME} PRIVATE APP_VERSION_MAJOR=\"${PROJECT_VERSION_MAJOR}\")
target_compile_definitions(${PROJECT_NAME} PRIVATE APP_VERSION_MINOR=\"${PROJECT_VERSION_MINOR}\")
target_compile_definitions(${PROJECT_NAME} PRIVATE APP_VERSION_PATCH=\"${PROJECT_VERSION_PATCH}\")
target_compile_definitions(${PROJECT_NAME} PRIVATE APP_BRANCH=\"${PROJECT_BRANCH}\")
target_compile_definitions(${PROJECT_NAME} PRIVATE APP_COMMIT_HASH=\"${PROJECT_COMMIT_HASH}\")
target_compile_definitions(${PROJECT_NAME} PRIVATE APP_BUILD_DATE=\"${PROJECT_BUILD_DATE}\")
target_compile_definitions(${PROJECT_NAME} PRIVATE APP_ROOT_DIR=\"${PROJECT_ROOT_DIR}\")

if (MSVC)
  target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive-)
else()
  target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()