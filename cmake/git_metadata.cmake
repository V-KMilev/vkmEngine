# Only run this once
if (NOT DEFINED PROJECT_BUILD_DATE)
    string(TIMESTAMP PROJECT_BUILD_DATE "%Y-%m-%d %H:%M:%S" UTC)
endif()

# Use Git to get branch and commit hash
if (NOT DEFINED PROJECT_BRANCH OR NOT DEFINED PROJECT_COMMIT_HASH)
    find_package(Git QUIET)
    if (GIT_FOUND)
        if (NOT DEFINED PROJECT_BRANCH)
            execute_process(
                COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE PROJECT_BRANCH
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
        endif()

        if (NOT DEFINED PROJECT_COMMIT_HASH)
            execute_process(
                COMMAND ${GIT_EXECUTABLE} rev-parse --short=8 HEAD
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE PROJECT_COMMIT_HASH
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
        endif()
    endif()
endif()

# Set defaults if Git not found or values missing
if (NOT DEFINED PROJECT_BRANCH OR PROJECT_BRANCH STREQUAL "")
    set(PROJECT_BRANCH "unknown")
endif()

if (NOT DEFINED PROJECT_COMMIT_HASH OR PROJECT_COMMIT_HASH STREQUAL "")
    set(PROJECT_COMMIT_HASH "00000000")
endif()

# Store metadata as cache variables (optional, for use in build system or source files)
set(PROJECT_BRANCH ${PROJECT_BRANCH} CACHE INTERNAL "Git branch")
set(PROJECT_COMMIT_HASH ${PROJECT_COMMIT_HASH} CACHE INTERNAL "Git commit hash")
set(PROJECT_BUILD_DATE ${PROJECT_BUILD_DATE} CACHE INTERNAL "Build date")