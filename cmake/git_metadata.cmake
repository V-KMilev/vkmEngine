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

# Per-module metadata for the About dialog and the build banner. Each vkm module
# is its own repository, so "which commit is this build made of" needs one hash
# per module rather than one for the tree. Versions are read out of each
# module's CMakeLists because vkm_build_info is defined before add_subdirectory(),
# where the <name>_VERSION variables do not exist yet.
function(vkm_module_metadata MODULE_PATH OUT_PREFIX)
    set(_hash "00000000")
    set(_version "unknown")

    if(GIT_FOUND AND EXISTS "${MODULE_PATH}")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short=8 HEAD
            WORKING_DIRECTORY ${MODULE_PATH}
            OUTPUT_VARIABLE _h
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(_h)
            set(_hash ${_h})
        endif()
    endif()

    if(EXISTS "${MODULE_PATH}/CMakeLists.txt")
        file(READ "${MODULE_PATH}/CMakeLists.txt" _txt)
        string(REGEX MATCH "PROJECT_VERSION_MAJOR[ \t]+([0-9]+)" _m "${_txt}")
        set(_major ${CMAKE_MATCH_1})
        string(REGEX MATCH "PROJECT_VERSION_MINOR[ \t]+([0-9]+)" _m "${_txt}")
        set(_minor ${CMAKE_MATCH_1})
        string(REGEX MATCH "PROJECT_VERSION_PATCH[ \t]+([0-9]+)" _m "${_txt}")
        set(_patch ${CMAKE_MATCH_1})
        if(DEFINED _major AND DEFINED _minor AND DEFINED _patch)
            set(_version "${_major}.${_minor}.${_patch}")
        endif()
    endif()

    set(${OUT_PREFIX}_COMMIT_HASH ${_hash}    PARENT_SCOPE)
    set(${OUT_PREFIX}_VERSION     ${_version} PARENT_SCOPE)
endfunction()

vkm_module_metadata("${CMAKE_SOURCE_DIR}/modules/vkmGL"  VKM_GL)
vkm_module_metadata("${CMAKE_SOURCE_DIR}/modules/vkmLog" VKM_LOG)
