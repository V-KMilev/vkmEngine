# Run by ctest, through the list vkm_test_binary writes (tests/CMakeLists.txt): asks the binary for
# its tests and adds each as a ctest of its own. VKM_TEST_EXE is the binary, VKM_TEST_TARGET its
# target, VKM_TEST_LABEL a label every test of it carries besides its area ("gpu", or none),
# VKM_TEST_DIR the source tree and VKM_TEST_CMAKE cmake's path, which ctest does not set. A test that
# stands for a binary that cannot answer names the target, as the suite has two.

if(NOT EXISTS "${VKM_TEST_EXE}")
    # Listed as a failure, not dropped: a suite that did not build is not a suite that passed.
    set(_vkm_built "${VKM_TEST_TARGET} is built")
    add_test("${_vkm_built}" "${VKM_TEST_CMAKE}" -E echo "${VKM_TEST_EXE} is not built")
    set_tests_properties("${_vkm_built}" PROPERTIES WILL_FAIL ON LABELS "${VKM_TEST_LABEL}")
    return()
endif()

execute_process(
    COMMAND "${VKM_TEST_EXE}" --list
    OUTPUT_VARIABLE _vkm_tests
    ERROR_VARIABLE  _vkm_error
    RESULT_VARIABLE _vkm_status
    TIMEOUT 60
)
if(NOT _vkm_status EQUAL 0)
    set(_vkm_lists "${VKM_TEST_TARGET} lists its tests")
    add_test("${_vkm_lists}" "${VKM_TEST_EXE}" --list)
    set_tests_properties("${_vkm_lists}" PROPERTIES LABELS "${VKM_TEST_LABEL}")
    return()
endif()

string(REPLACE "\n" ";" _vkm_tests "${_vkm_tests}")
foreach(_vkm_test IN LISTS _vkm_tests)
    if(_vkm_test STREQUAL "")
        continue()
    endif()
    string(REGEX MATCH "^[^:]+" _vkm_area "${_vkm_test}")
    add_test("${_vkm_test}" "${VKM_TEST_EXE}" "${_vkm_test}")
    set_tests_properties("${_vkm_test}" PROPERTIES
        LABELS            "${_vkm_area};${VKM_TEST_LABEL}"
        SKIP_RETURN_CODE  77
        TIMEOUT           300
        WORKING_DIRECTORY "${VKM_TEST_DIR}"
    )
endforeach()
