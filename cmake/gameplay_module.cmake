# vkm_add_gameplay_module(<name> [SOURCES <files>])
#
# The recipe for the one library a project loads, shared by user projects and the
# engine's examples, so it names only vkmEngine:: targets.
#
# The project is the calling directory; without SOURCES the module is every .cpp
# under its src/, which is also its include root. It is always named
# `game`, the name a host loads, and lands in bin/ or VKM_MODULE_DIR.

# vkm_gameplay_module_options(<target>)
#
# What a library needs to be loaded and unloaded by a host.
function(vkm_gameplay_module_options TARGET)
    # The engine, never a host: every host loads this one file.
    target_link_libraries(${TARGET} PRIVATE vkmEngine::vkm_core)

    # GCC marks inline and template statics STB_GNU_UNIQUE, and glibc never
    # unmaps a library that has one, so a reload would bind to the old copy.
    target_compile_options(${TARGET} PRIVATE $<$<CXX_COMPILER_ID:GNU>:-fno-gnu-unique>)

    # No rpath: the host has loaded the engine, and the loader matches it by name.
    set_target_properties(${TARGET} PROPERTIES SKIP_BUILD_RPATH ON)
endfunction()

# vkm_check_engine_version(<project dir>)
#
# Refuses an engine of another minor release than project.json's engineVersion,
# the one record of it: the engine is not ABI-stable across them.
function(vkm_check_engine_version PROJECT_DIR)
    set(_file ${PROJECT_DIR}/project.json)
    if(NOT EXISTS ${_file})
        return()
    endif()
    # An edit to it configures again.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_file})

    file(READ ${_file} _json)
    string(JSON _wanted ERROR_VARIABLE _unreadable GET "${_json}" engineVersion)
    if(_unreadable OR NOT _wanted)
        return()
    endif()

    string(REGEX MATCH "^[0-9]+\\.[0-9]+" _wanted_minor "${_wanted}")
    string(REGEX MATCH "^[0-9]+\\.[0-9]+" _have_minor "${vkmEngine_VERSION}")
    if(NOT _wanted_minor STREQUAL _have_minor)
        message(FATAL_ERROR
            "${_file} was made for vkmEngine ${_wanted}, and this is ${vkmEngine_VERSION}.\n"
            "The engine is not ABI-stable across minor releases, so a module is built against "
            "the engine it was made for. To move the project to this one, set engineVersion "
            "in project.json to ${vkmEngine_VERSION}."
        )
    endif()
endfunction()

function(vkm_add_gameplay_module TARGET)
    cmake_parse_arguments(VKM "" "" "SOURCES" ${ARGN})
    set(VKM_PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
    if(NOT VKM_SOURCES)
        file(GLOB_RECURSE VKM_SOURCES CONFIGURE_DEPENDS ${VKM_PROJECT_DIR}/src/*.cpp)
    endif()
    if(NOT VKM_SOURCES)
        message(FATAL_ERROR "vkm_add_gameplay_module(${TARGET}): no .cpp under ${VKM_PROJECT_DIR}/src")
    endif()

    vkm_check_engine_version(${VKM_PROJECT_DIR})

    add_library(${TARGET} SHARED ${VKM_SOURCES})
    vkm_gameplay_module_options(${TARGET})

    target_include_directories(${TARGET} PRIVATE ${VKM_PROJECT_DIR}/src)

    # bin/ is where a host looks; `vkm package` names its own for a shipping module.
    set(_dir ${VKM_PROJECT_DIR}/bin)
    if(VKM_MODULE_DIR)
        set(_dir ${VKM_MODULE_DIR})
    endif()

    # The archive too, or Windows import libraries of two projects collide. No debug
    # postfix: the host loads the module by a name no configuration may change.
    set_target_properties(${TARGET} PROPERTIES
        OUTPUT_NAME              game
        DEBUG_POSTFIX            ""
        RUNTIME_OUTPUT_DIRECTORY ${_dir}
        LIBRARY_OUTPUT_DIRECTORY ${_dir}
        ARCHIVE_OUTPUT_DIRECTORY ${_dir}
    )
    if(WIN32)
        set_target_properties(${TARGET} PROPERTIES PREFIX "")
    endif()
endfunction()
