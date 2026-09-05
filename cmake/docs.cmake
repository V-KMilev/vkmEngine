# The generated API reference.
#
# Optional: doxygen is not a build dependency, and its absence is not an error -
# it means the `docs` target is not offered. Nothing else in the build looks at
# it. Run with `cmake --build build --target docs`; the site lands in
# build/docs/html and the undocumented-symbol warnings in
# build/docs/doxygen-warnings.log, which is the file worth reading after a
# release: it is the list of public surface that shipped without a sentence.
find_package(Doxygen QUIET)

if(NOT DOXYGEN_FOUND)
    message(STATUS "Doxygen not found - the `docs` target is unavailable")
    return()
endif()

set(DOXYGEN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/docs")
if(TARGET Doxygen::dot)
    set(DOXYGEN_HAVE_DOT YES)
else()
    set(DOXYGEN_HAVE_DOT NO)
endif()

configure_file(
    "${CMAKE_SOURCE_DIR}/docs/Doxyfile.in"
    "${CMAKE_BINARY_DIR}/Doxyfile"
    @ONLY
)

add_custom_target(docs
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DOXYGEN_OUTPUT_DIR}"
    COMMAND Doxygen::doxygen "${CMAKE_BINARY_DIR}/Doxyfile"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Generating the API reference into ${DOXYGEN_OUTPUT_DIR}/html"
    VERBATIM
)

message(STATUS "Doxygen ${DOXYGEN_VERSION} found - `docs` target available")
