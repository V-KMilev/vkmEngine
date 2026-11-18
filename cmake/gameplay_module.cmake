# vkm_add_gameplay_module(<name>)
#
# The recipe for the one library a project loads, shared by user projects and the
# engine's examples, so it names only vkmEngine:: targets.
#
# The project is the calling directory; the module is every .cpp under its src/,
# which is also its include root. It is always named `game`, the name a host
# loads, and lands in bin/, or VKM_MODULE_DIR, or for an example of an engine tree
# but build/ beside that tree's hosts (VKM_TREE_EXAMPLE_MODULES, CMakeLists.txt).

# vkm_gameplay_module_options(<target>)
#
# What a library needs to be loaded and unloaded by a host.
function(vkm_gameplay_module_options TARGET)
    # The engine, never a host: every host loads this one file.
    target_link_libraries(${TARGET} PRIVATE vkmEngine::vkm_core)

    # GCC marks inline and template statics STB_GNU_UNIQUE, and glibc never
    # unmaps a library that has one, so a reload would bind to the old copy.
    target_compile_options(${TARGET} PRIVATE $<$<CXX_COMPILER_ID:GNU>:-fno-gnu-unique>)

    # Vectorized as GCC's -O2 does, as the engine is: its -O3 cost model takes
    # loops it then compiles to wrong results (the engine's CMakeLists.txt).
    target_compile_options(${TARGET} PRIVATE $<$<CXX_COMPILER_ID:GNU>:-fvect-cost-model=very-cheap>)

    # Every file includes the engine's gameplay surface, compiled once. The log category is the
    # build's, as a file's own #define would redefine the precompiled header's.
    target_precompile_headers(${TARGET} PRIVATE <system/script/behavior_api.h>)
    target_compile_definitions(${TARGET} PRIVATE VKM_LOG_CATEGORY="GAME")

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
    if(ARGN)
        message(FATAL_ERROR "vkm_add_gameplay_module(${TARGET}): takes a name alone, not '${ARGN}'")
    endif()
    set(VKM_PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
    file(GLOB_RECURSE VKM_SOURCES CONFIGURE_DEPENDS ${VKM_PROJECT_DIR}/src/*.cpp)
    if(NOT VKM_SOURCES)
        message(FATAL_ERROR "vkm_add_gameplay_module(${TARGET}): no .cpp under ${VKM_PROJECT_DIR}/src")
    endif()

    vkm_check_engine_version(${VKM_PROJECT_DIR})

    add_library(${TARGET} SHARED ${VKM_SOURCES})
    vkm_gameplay_module_options(${TARGET})

    target_include_directories(${TARGET} PRIVATE ${VKM_PROJECT_DIR}/src)

    # bin/ is where a host looks; `vkm package` names its own for a shipping module, and a tree
    # but build/ puts an example's in bin/examples/<name>/ beside its hosts, the one place they
    # look for it, whatever its build type.
    set(_dir ${VKM_PROJECT_DIR}/bin)
    if(VKM_MODULE_DIR)
        set(_dir ${VKM_MODULE_DIR})
    elseif(VKM_TREE_EXAMPLE_MODULES)
        get_filename_component(_name ${VKM_PROJECT_DIR} NAME)
        set(_dir $<TARGET_FILE_DIR:vkm_core>/examples/${_name})
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

    # Stamped once linked, so the editor reloads a whole library the moment one lands
    # (ReloadWatcher); a shipping module is never reloaded.
    if(NOT VKM_MODULE_DIR)
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E touch $<TARGET_FILE_DIR:${TARGET}>/game.built
            VERBATIM
        )
    endif()
endfunction()
