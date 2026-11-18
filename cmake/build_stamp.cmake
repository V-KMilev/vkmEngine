# The build's git metadata, written into OUT at every build by the vkm_build_stamp target, so a binary
# names the commits it was built from: the branch, and each repository's short commit and version.
# Written only when one of them changed, with that moment as the build date, so a build that brings
# nothing new recompiles nothing.
#   cmake -DSOURCE_DIR=<engine> -DGIT=<git, or empty> -DOUT=<header> -P build_stamp.cmake

function(git_output DIR OUT_VAR DEFAULT)
    set(_value "${DEFAULT}")
    if(GIT AND EXISTS "${DIR}")
        execute_process(
            COMMAND ${GIT} ${ARGN}
            WORKING_DIRECTORY ${DIR}
            OUTPUT_VARIABLE _out
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(_out)
            set(_value "${_out}")
        endif()
    endif()
    set(${OUT_VAR} "${_value}" PARENT_SCOPE)
endfunction()

# A module's version, from its own CMakeLists: its version variables are not this script's.
function(module_version DIR OUT_VAR)
    set(_version "unknown")
    if(EXISTS "${DIR}/CMakeLists.txt")
        file(READ "${DIR}/CMakeLists.txt" _txt)
        string(REGEX MATCH "PROJECT_VERSION_MAJOR[ \t]+([0-9]+)" _m "${_txt}")
        set(_major ${CMAKE_MATCH_1})
        string(REGEX MATCH "PROJECT_VERSION_MINOR[ \t]+([0-9]+)" _m "${_txt}")
        set(_minor ${CMAKE_MATCH_1})
        string(REGEX MATCH "PROJECT_VERSION_PATCH[ \t]+([0-9]+)" _m "${_txt}")
        set(_patch ${CMAKE_MATCH_1})
        if(NOT "${_major}" STREQUAL "" AND NOT "${_minor}" STREQUAL "" AND NOT "${_patch}" STREQUAL "")
            set(_version "${_major}.${_minor}.${_patch}")
        endif()
    endif()
    set(${OUT_VAR} "${_version}" PARENT_SCOPE)
endfunction()

git_output("${SOURCE_DIR}" _branch "unknown" rev-parse --abbrev-ref HEAD)
git_output("${SOURCE_DIR}" _commit "00000000" rev-parse --short=8 HEAD)
set(_lines
    "#define APP_BRANCH \"${_branch}\"\n"
    "#define APP_COMMIT_HASH \"${_commit}\"\n"
)
foreach(_module Log Platform Audio Network Physics OpenGL)
    string(TOUPPER ${_module} _name)
    set(_dir "${SOURCE_DIR}/modules/vkm${_module}")
    git_output("${_dir}" _hash "00000000" rev-parse --short=8 HEAD)
    module_version("${_dir}" _version)
    list(APPEND _lines
        "#define VKM_${_name}_VERSION \"${_version}\"\n"
        "#define VKM_${_name}_COMMIT_HASH \"${_hash}\"\n"
    )
endforeach()
string(CONCAT _stamp ${_lines})

# The date line is the last; what is above it decides whether the file is written again.
set(_head "#pragma once\n\n// Written by cmake/build_stamp.cmake when a commit or a branch changes it.\n")
if(EXISTS "${OUT}")
    file(READ "${OUT}" _old)
    string(FIND "${_old}" "#define APP_BUILD_DATE" _at)
    if(_at GREATER_EQUAL 0)
        string(SUBSTRING "${_old}" 0 ${_at} _old)
    endif()
    if("${_old}" STREQUAL "${_head}${_stamp}")
        return()
    endif()
endif()
string(TIMESTAMP _date "%Y-%m-%d %H:%M:%S" UTC)
file(WRITE "${OUT}" "${_head}${_stamp}#define APP_BUILD_DATE \"${_date}\"\n")
