# vkm_add_gameplay_module(<name> SOURCES <files> [OUTPUT_NAME <n>] [PROJECT_DIR <d>])
#
# The one recipe for the one shared library a project loads. Every project's
# module is built the same way, so the SDK provides it rather than asking each
# project to restate it - and the engine's own examples call it too, so the
# recipe a user gets is the recipe we test.
#
# Included twice: by the top-level CMakeLists for the examples in this tree, and
# by vkmEngineConfig.cmake for a project that found the engine. It names only
# vkmEngine:: targets, which exist in both.

function(vkm_add_gameplay_module TARGET)
    cmake_parse_arguments(VKM "" "OUTPUT_NAME;PROJECT_DIR" "SOURCES" ${ARGN})

    if(NOT VKM_SOURCES)
        message(FATAL_ERROR "vkm_add_gameplay_module(${TARGET}): SOURCES is required")
    endif()
    if(NOT VKM_OUTPUT_NAME)
        set(VKM_OUTPUT_NAME game)
    endif()
    if(NOT VKM_PROJECT_DIR)
        set(VKM_PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
    endif()

    add_library(${TARGET} SHARED ${VKM_SOURCES})

    # Links the engine, never a host executable: the editor and the runtime load
    # the same file.
    target_link_libraries(${TARGET} PRIVATE vkmEngine::vkm_core)

    # Into the project's own bin/, which is the one place a host looks.
    # All three destinations, because Windows splits a shared library into
    # game.dll and the import library libgame.dll.a. Leaving the archive to a
    # global default puts every project's import library in one directory under
    # the same name, and two projects configured in one tree collide there.
    set_target_properties(${TARGET} PROPERTIES
        OUTPUT_NAME              ${VKM_OUTPUT_NAME}
        RUNTIME_OUTPUT_DIRECTORY ${VKM_PROJECT_DIR}/bin
        LIBRARY_OUTPUT_DIRECTORY ${VKM_PROJECT_DIR}/bin
        ARCHIVE_OUTPUT_DIRECTORY ${VKM_PROJECT_DIR}/bin
    )
    if(WIN32)
        set_target_properties(${TARGET} PROPERTIES PREFIX "")
    endif()
endfunction()
