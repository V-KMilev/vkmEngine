# The SDK install - every install rule the engine has, in one file.
#
# `cmake --install` produces an SDK, not a game. That distinction is the whole
# point of the project split: the engine is a thing you build games with, and a
# game is a project directory plus a renamed copy of vkm_runtime. Packaging a
# game is a separate operation (see tools/vkm), not a mode of this one.
#
# The layout a project sees:
#
#   <prefix>/bin/      the three hosts and the shared libraries they need
#   <prefix>/include/  the engine's public headers, module-qualified, plus the
#                      third-party headers its public headers reach into
#   <prefix>/lib/cmake/vkmEngine/   find_package() lands here
#   <prefix>/shaders/  engine shaders, loaded at run time relative to the root
#   <prefix>/templates/ what `vkm new` copies

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(VKM_CMAKE_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/vkmEngine)

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
# A project links exactly one engine target: vkm_core, through the
# vkm_add_gameplay_module() helper the config file defines. Everything else the
# engine is made of - the render system, the GL backend, the cooker, the editor -
# is reached by running a host, not by linking, so none of it belongs in the
# export set. What has to be here besides vkm_core is its own link interface:
# CMake refuses to export a target whose dependencies are not exported with it,
# and for a shared library that includes the private ones.
set(VKM_EXPORTED_DEPS glm glm-header-only glfw vkm_log nlohmann_json glew stb BuildInfo vkm_warnings)
if(VKM_PROFILER)
    list(APPEND VKM_EXPORTED_DEPS TracyClient)
endif()

install(TARGETS vkm_core ${VKM_EXPORTED_DEPS}
        EXPORT  vkmEngineTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime
        LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Development
)

# Shared libraries the hosts load but a project never links: they ship as files,
# not as imported targets.
install(TARGETS vkm_render vkm_gl
        LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)

# The hosts. A packaged game is a renamed copy of vkm_runtime, so the binary
# ships in the SDK rather than being rebuilt per game - that is the point of the
# project split.
install(TARGETS vkm_runtime_app vkm_editor_app vkm_cook_app
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)

# ---------------------------------------------------------------------------
# Headers
# ---------------------------------------------------------------------------
# src/engine ships and nothing else does: a project writes behaviors and builds
# scenes, and reaches neither the GL backend nor the editor. The directory
# structure is the include path, so it is preserved exactly - "ecs/scene.h"
# resolves the same against the SDK as it does in the engine tree.
install(DIRECTORY ${CMAKE_SOURCE_DIR}/src/engine/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        COMPONENT Development
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# Third-party headers the engine's own public headers include. An audit of all
# 121 public headers found these and nothing else, so this list is the complete
# set a project needs - not a guess.
install(DIRECTORY ${CMAKE_SOURCE_DIR}/modules/glm/glm
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT Development
        FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h" PATTERN "*.inl")
install(DIRECTORY ${CMAKE_SOURCE_DIR}/modules/json/single_include/nlohmann
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT Development)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/modules/glfw/include/GLFW
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT Development)

# No GL headers: the one public header that included glew was the GPU profiler,
# and it belongs to the backend rather than the engine. Nothing a project can
# reach names an OpenGL type.
install(FILES ${CMAKE_SOURCE_DIR}/modules/vkmLog/src/logger.h
              ${CMAKE_SOURCE_DIR}/modules/vkmLog/src/l_assert.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT Development)

# Only when the engine was built with it: debug/profiler.h includes Tracy's
# header behind VKM_PROFILER, and that define travels in the export.
if(VKM_PROFILER)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/modules/tracy/public/tracy
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT Development
            FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
endif()

# ---------------------------------------------------------------------------
# Engine data
# ---------------------------------------------------------------------------
# Shaders are engine chrome: they ship with the engine and a project never edits
# them. Everything a project owns - its scenes, its assets, its cooked library -
# belongs to the project and is not the SDK's to install.
# _generated included, not excluded: configure writes it into the SOURCE tree
# (cmake/generate_shader_config.cmake), and the shaders #include from it - so a
# package without it compiles nothing. It is gitignored because it is derived,
# which is a different question from whether it ships.
install(DIRECTORY ${CMAKE_SOURCE_DIR}/shaders
        DESTINATION . COMPONENT Runtime)

# The editor's own font and icon. Engine chrome, unlike a project's art.
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/fonts ${CMAKE_SOURCE_DIR}/assets/logo
        DESTINATION assets COMPONENT Runtime OPTIONAL)

# What `vkm new` copies to make a project.
install(DIRECTORY ${CMAKE_SOURCE_DIR}/templates
        DESTINATION . COMPONENT Development)

# The compiler's own runtime, which is not an imported target so CMake cannot
# carry it automatically. The engine's shared libraries link libstdc++
# dynamically - only targets that link assimp inherit its -static-libstdc++ - so
# without these a package fails to start with a missing-DLL box and no log.
if(MINGW)
    get_filename_component(_vkm_mingw_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(_dll libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
        if(EXISTS "${_vkm_mingw_bin}/${_dll}")
            install(FILES "${_vkm_mingw_bin}/${_dll}"
                    DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)
        endif()
    endforeach()
endif()

# The command-line front end, so a user never writes CMake.
install(PROGRAMS ${CMAKE_SOURCE_DIR}/tools/vkm
        DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)

# ---------------------------------------------------------------------------
# The package
# ---------------------------------------------------------------------------
install(EXPORT vkmEngineTargets
        FILE        vkmEngineTargets.cmake
        NAMESPACE   vkmEngine::
        DESTINATION ${VKM_CMAKE_INSTALL_DIR}
        COMPONENT   Development)

configure_package_config_file(
    ${CMAKE_SOURCE_DIR}/cmake/vkmEngineConfig.cmake.in
    ${CMAKE_BINARY_DIR}/vkmEngineConfig.cmake
    INSTALL_DESTINATION ${VKM_CMAKE_INSTALL_DIR}
)

# SameMajorVersion: the engine is not ABI-stable across minor releases, but a
# project asking for 1.4 should accept 1.4.2. The toolchain pin in the config
# file is what catches the case this cannot.
write_basic_package_version_file(
    ${CMAKE_BINARY_DIR}/vkmEngineConfigVersion.cmake
    VERSION       ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion
)

install(FILES
            ${CMAKE_BINARY_DIR}/vkmEngineConfig.cmake
            ${CMAKE_BINARY_DIR}/vkmEngineConfigVersion.cmake
        DESTINATION ${VKM_CMAKE_INSTALL_DIR}
        COMPONENT   Development)
