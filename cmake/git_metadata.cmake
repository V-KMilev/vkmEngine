# Build metadata embedded in the binary: branch, short commit, and a configure
# timestamp. Recomputed on every configure so it tracks the current checkout -
# re-run cmake to refresh after committing or switching branches. (For
# per-build accuracy you'd generate a header in a build-time custom command;
# configure-time is enough for this project.)

string(TIMESTAMP PROJECT_BUILD_DATE "%Y-%m-%d %H:%M:%S" UTC)

set(PROJECT_BRANCH      "unknown")
set(PROJECT_COMMIT_HASH "00000000")

find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE _git_branch
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short=8 HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE _git_commit
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(_git_branch)
        set(PROJECT_BRANCH ${_git_branch})
    endif()
    if(_git_commit)
        set(PROJECT_COMMIT_HASH ${_git_commit})
    endif()
endif()
