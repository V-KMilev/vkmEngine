# The engine's tests. A test is TEST("what works") in tests/<area>/<topic>.cpp, the area being the
# folder of the engine it tests (harness/check.h). Each runs in a process of its own and is a ctest
# of its own, named "<area>: <what works>" and labeled with its area, so `ctest -R` picks by
# promise, `ctest -L '^physics$'` by area and `ctest -j4` runs them side by side.
#
# Built with the engine, because a suite that is opt-in is a suite nobody runs. It needs no GPU and
# no display: it links the engine, the asset pipeline and the editor's library, none of which names
# a graphics API (the editor's GL renderer is the OpenGL backend's). The GPU tests are a binary of
# their own below.

# Where the harness and the areas are, for a binary in a folder below them too (render/<api>/).
set(VKM_TESTS_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Registers every test a binary lists as a ctest, when ctest runs: a test added is a test run, with
# no list to keep. A multi-config generator builds the binary once per build type, so each gets a list
# of its own, named in lower case as ctest -C matches a build type in any case; the file ctest includes
# reads the one -C names, or fails one test when -C names none of them, so the run goes on. A
# single-config generator's one list is that file itself.
function(vkm_test_binary TARGET LABEL)
    set(_include ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_tests.cmake)
    set(_list ${_include})
    get_property(_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(_multi_config)
        set(_list ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_tests-$<LOWER_CASE:$<CONFIG>>.cmake)
        string(REPLACE ";" ", " _types "${CMAKE_CONFIGURATION_TYPES}")
        file(WRITE ${_include}
            "string(TOLOWER \"\${CTEST_CONFIGURATION_TYPE}\" _vkm_config)\n"
            "set(_vkm_list \"${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_tests-\${_vkm_config}.cmake\")\n"
            "if(_vkm_config STREQUAL \"\" OR NOT EXISTS \"\${_vkm_list}\")\n"
            "    set(_vkm_named \"${TARGET}: ctest -C names a build type it has\")\n"
            "    add_test(\"\${_vkm_named}\" [==[${CMAKE_COMMAND}]==] -E echo \"run ctest -C, one of ${_types}\")\n"
            "    set_tests_properties(\"\${_vkm_named}\" PROPERTIES WILL_FAIL ON LABELS [==[${LABEL}]==])\n"
            "    return()\n"
            "endif()\n"
            "include(\"\${_vkm_list}\")\n"
        )
    endif()
    string(CONCAT _content
        "set(VKM_TEST_EXE [==[$<TARGET_FILE:${TARGET}>]==])\n"
        "set(VKM_TEST_TARGET ${TARGET})\n"
        "set(VKM_TEST_LABEL [==[${LABEL}]==])\n"
        "set(VKM_TEST_DIR [==[${CMAKE_SOURCE_DIR}]==])\n"
        "set(VKM_TEST_CMAKE [==[${CMAKE_COMMAND}]==])\n"
        "include([==[${VKM_TESTS_DIR}/harness/discover.cmake]==])\n"
    )
    file(GENERATE OUTPUT ${_list} CONTENT "${_content}")
    set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES ${_include})
    target_include_directories(${TARGET} PRIVATE ${VKM_TESTS_DIR} ${CMAKE_SOURCE_DIR})
endfunction()

file(GLOB VKM_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*/*.cpp)
list(FILTER VKM_TEST_SOURCES EXCLUDE REGEX "/tests/(harness|fixture|determinism)/")

# The runtime's bench is built in too: tests/e2e/bench.cpp runs it on a game with no display.
add_executable(vkm_engine_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/harness/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/harness/world.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/harness/lockstep.cpp
    ${CMAKE_SOURCE_DIR}/app/runtime/bench_system.cpp
    ${VKM_TEST_SOURCES}
)
target_link_libraries(vkm_engine_tests PRIVATE
    vkm_editor
    vkm_pipeline
    vkm_loader
    vkm_render
    vkmEngine::vkm_core
    vkmEngine::vkm_warnings
)
vkm_test_binary(vkm_engine_tests "")

# The gameplay modules the tests load (tests/fixture/): `game`, built as a project's is and twice, so a
# reload can be seen running the new build; and the ones a host refuses or plays only offline. Built
# outside bin/, which `vkm package` carries into a game; a test copies one into its project's bin/.
function(vkm_fixture_module NAME SOURCE)
    add_library(vkm_fixture_${NAME} MODULE ${CMAKE_CURRENT_SOURCE_DIR}/fixture/${SOURCE})
    target_compile_definitions(vkm_fixture_${NAME} PRIVATE ${ARGN})
    set_target_properties(vkm_fixture_${NAME} PROPERTIES
        OUTPUT_NAME              ${NAME}
        DEBUG_POSTFIX            ""
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test_fixtures
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test_fixtures
    )
    if(WIN32)
        set_target_properties(vkm_fixture_${NAME} PROPERTIES PREFIX "")
    endif()
    add_dependencies(vkm_engine_tests vkm_fixture_${NAME})
endfunction()

foreach(_build 1 2)
    vkm_fixture_module(game_v${_build} game.cpp VKM_FIXTURE_BUILD=${_build})
    vkm_gameplay_module_options(vkm_fixture_game_v${_build})
endforeach()
vkm_fixture_module(game_stale refused.cpp VKM_FIXTURE_VERSION="0.0.0" VKM_FIXTURE_REGISTERS)
vkm_fixture_module(game_unversioned refused.cpp VKM_FIXTURE_REGISTERS)
vkm_fixture_module(game_unregistering refused.cpp VKM_FIXTURE_VERSION=VKM_ENGINE_VERSION)
vkm_fixture_module(game_offline refused.cpp VKM_FIXTURE_VERSION=VKM_ENGINE_VERSION VKM_FIXTURE_REGISTERS)
foreach(_refused stale unversioned unregistering offline)
    target_link_libraries(vkm_fixture_game_${_refused} PRIVATE vkmEngine::vkm_core)
endforeach()

# The hosts the end-to-end tests run as a user does, built before the tests that run them.
add_dependencies(vkm_engine_tests vkm_cook_app vkm_runtime_app vkm_server_app)
target_compile_definitions(vkm_engine_tests PRIVATE
    VKM_TEST_MODULE_DIR="$<TARGET_FILE_DIR:vkm_fixture_game_v1>"
    VKM_TEST_MODULE_PREFIX="$<TARGET_FILE_PREFIX:vkm_fixture_game_v1>"
    VKM_TEST_MODULE_SUFFIX="$<TARGET_FILE_SUFFIX:vkm_fixture_game_v1>"
    VKM_TEST_HOST_DIR="$<TARGET_FILE_DIR:vkm_cook_app>"
    VKM_TEST_HOST_SUFFIX="${CMAKE_EXECUTABLE_SUFFIX}"
    VKM_TEST_EXAMPLE_MODULE="$<TARGET_FILE:the_lab_module>"
)

# Determinism: the lab and the arena, each cooked first, played 600 ticks at 1, 2 and 3 steps a frame
# must hash as recorded (tests/determinism/physics_hash.cpp), and log no error. A change that is meant to
# move a body - the physics, a library's pin, an example - records the new hash here.
add_executable(vkm_physics_hash ${CMAKE_CURRENT_SOURCE_DIR}/determinism/physics_hash.cpp)
target_link_libraries(vkm_physics_hash PRIVATE
    vkm_render
    vkm_loader
    vkmEngine::vkm_core
    vkm_build_info
    vkmEngine::vkm_warnings
)
target_include_directories(vkm_physics_hash PRIVATE ${CMAKE_SOURCE_DIR})
add_dependencies(vkm_physics_hash the_lab_module stress_arena_module)
set(_vkm_logged_error "\\[(ERROR|FATAL)\\]")
foreach(_run "the_lab 7dd0ecc88244a6e1" "stress_arena 1f0c8224b31c51e8")
    separate_arguments(_run)
    list(GET _run 0 _example)
    list(GET _run 1 _hash)
    set(_project ${CMAKE_SOURCE_DIR}/examples/${_example})
    add_test(NAME "determinism: ${_example} is cooked" COMMAND $<TARGET_FILE:vkm_cook_app> ${_project})
    set_tests_properties("determinism: ${_example} is cooked" PROPERTIES
        LABELS                  determinism
        FIXTURES_SETUP          cooked_${_example}
        FAIL_REGULAR_EXPRESSION ${_vkm_logged_error}
        TIMEOUT                 1800
    )
    foreach(_steps 1 2 3)
        set(_name "determinism: ${_example} hashes as recorded at ${_steps} steps a frame")
        add_test(NAME ${_name} COMMAND $<TARGET_FILE:vkm_physics_hash> ${_project} 600 ${_steps} ${_hash})
        set_tests_properties(${_name} PROPERTIES
            LABELS                  determinism
            FIXTURES_REQUIRED       cooked_${_example}
            FAIL_REGULAR_EXPRESSION ${_vkm_logged_error}
            TIMEOUT                 600
        )
    endforeach()
endforeach()

# The project tool, run as a user runs it: a ctest for each file of tests/cli/ (one whose name starts
# with `_` is a helper). A test that drives the engine's build skips unless this is the build vkm finds.
find_package(Python3 3.8 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    target_compile_definitions(vkm_engine_tests PRIVATE VKM_TEST_PYTHON="${Python3_EXECUTABLE}")
    file(GLOB VKM_CLI_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cli/*.py)
    list(FILTER VKM_CLI_TESTS EXCLUDE REGEX "/_[^/]*$")
    foreach(_test IN LISTS VKM_CLI_TESTS)
        get_filename_component(_name ${_test} NAME_WE)
        add_test(NAME "cli: ${_name}" COMMAND ${Python3_EXECUTABLE} ${_test})
        set_tests_properties("cli: ${_name}" PROPERTIES
            LABELS           cli
            SKIP_RETURN_CODE 77
            TIMEOUT          600
            ENVIRONMENT      "VKM_TEST_BUILD=${CMAKE_BINARY_DIR};VKM_TEST_SCRATCH=${CMAKE_BINARY_DIR}/test_scratch/cli"
        )
    endforeach()

    # The docs and the tree agree where a machine can tell (tests/docs/check_docs.py).
    add_test(NAME "docs: the docs and the tree agree"
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/docs/check_docs.py ${CMAKE_SOURCE_DIR})
    set_tests_properties("docs: the docs and the tree agree" PROPERTIES LABELS docs TIMEOUT 120)
endif()

# GL's error check is compiled into a Debug build alone, whatever the generator (modules/CMakeLists.txt):
# vkmOpenGL's objects call it in the configuration ctest -C names exactly when that is Debug.
if(CMAKE_NM)
    set(_name "build: GL's error check is compiled into a Debug build alone")
    add_test(NAME ${_name}
        COMMAND ${CMAKE_COMMAND} "-DOBJECTS=$<JOIN:$<TARGET_OBJECTS:vkm_opengl>,|>" -DNM=${CMAKE_NM}
            -DCHECKS=$<CONFIG:Debug> -P ${CMAKE_SOURCE_DIR}/modules/vkmOpenGL/tests/error_check.cmake)
    set_tests_properties(${_name} PROPERTIES LABELS build)
endif()

# A multi-config tree that took VKM_PROFILER's default carries no Tracy in any build type (CMakeLists.txt):
# no object of vkm_core calls into it.
if(CMAKE_NM AND _vkm_multi_config AND VKM_PROFILER_DEFAULTED)
    set(_name "build: a multi-config build carries no Tracy unless asked")
    add_test(NAME ${_name}
        COMMAND ${CMAKE_COMMAND} "-DOBJECTS=$<JOIN:$<TARGET_OBJECTS:vkm_core>,|>" -DNM=${CMAKE_NM}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/build/no_tracy.cmake)
    set_tests_properties(${_name} PROPERTIES LABELS build)
endif()

# The GPU tests, a binary for each graphics backend (tests/render/<api>/).
add_subdirectory(render/opengl)
