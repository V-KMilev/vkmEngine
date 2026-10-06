# The vkm_tool test: new, doctor, package and clean, on a project of its own.
# PYTHON, VKM, WORK and EXE come from tests/CMakeLists.txt.

# Runs vkm, leaving its exit code in rc and its output in out.
function(vkm_run)
    execute_process(
        COMMAND ${PYTHON} ${VKM} ${ARGN}
        RESULT_VARIABLE _rc
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE  _out
    )
    set(rc  ${_rc}  PARENT_SCOPE)
    set(out ${_out} PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE ${WORK})
file(MAKE_DIRECTORY ${WORK})

vkm_run(--version)
if(rc OR NOT out MATCHES "vkmEngine [0-9]+\\.[0-9]+\\.[0-9]+")
    message(FATAL_ERROR "vkm --version did not name the engine:\n${out}")
endif()

vkm_run(new ${WORK}/game)
if(rc)
    message(FATAL_ERROR "vkm new failed:\n${out}")
endif()

file(READ ${WORK}/game/project.json _json)
string(JSON _name GET "${_json}" name)
string(JSON _engine GET "${_json}" engineVersion)
string(JSON _version GET "${_json}" version)
if(NOT _name STREQUAL "game" OR NOT _engine MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "vkm new stamped project.json wrong:\n${_json}")
endif()
foreach(_made CMakeLists.txt .gitignore src/module.cpp)
    if(NOT EXISTS ${WORK}/game/${_made})
        message(FATAL_ERROR "vkm new left out ${_made}")
    endif()
endforeach()

# Doctor's verdict is the machine's; that it runs and reads the project is the tool's.
vkm_run(doctor ${WORK}/game)
if(NOT out MATCHES "project game")
    message(FATAL_ERROR "vkm doctor did not read the project:\n${out}")
endif()

# On the development engine, which packages in seconds.
vkm_run(package ${WORK}/game --development -j 2)
if(rc)
    message(FATAL_ERROR "vkm package failed:\n${out}")
endif()
file(GLOB _packages LIST_DIRECTORIES true ${WORK}/game/dist/game-${_version}-*)
list(FILTER _packages EXCLUDE REGEX "-symbols$")
list(LENGTH _packages _count)
if(NOT _count EQUAL 1)
    message(FATAL_ERROR "vkm package left ${_count} packages named for version ${_version}:\n${out}")
endif()
foreach(_shipped project.json shaders bin/game${EXE} assets/logo)
    if(NOT EXISTS ${_packages}/${_shipped})
        message(FATAL_ERROR "the package is missing ${_shipped}")
    endif()
endforeach()
foreach(_left_behind assets/logo/_preview build src)
    if(EXISTS ${_packages}/${_left_behind})
        message(FATAL_ERROR "the package carries ${_left_behind}, which no game reads")
    endif()
endforeach()

vkm_run(clean ${WORK}/game --all)
if(rc OR EXISTS ${WORK}/game/build OR EXISTS ${WORK}/game/dist)
    message(FATAL_ERROR "vkm clean --all left the build or the packages:\n${out}")
endif()
