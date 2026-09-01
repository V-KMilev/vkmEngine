# The SDK install - every install rule the engine has, in one file.
#
# `cmake --install` produces an SDK, not a game: the engine is a thing you build
# games with, and a game is a project directory plus a renamed copy of
# vkm_runtime. Packaging a game is a separate operation (tools/vkm), not a mode
# of this one. The layout these rules produce is in docs/reference/building.md.

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(VKM_CMAKE_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/vkmEngine)

# A project links exactly one engine target: vkm_core, through the
# vkm_add_gameplay_module() helper the config file includes. Everything else the
# engine is made of - the render system, the GL backend, the cooker, the editor -
# is reached by running a host, not by linking, so none of it belongs in the
# export set. What has to be here besides vkm_core is its own link interface:
# CMake refuses to export a target whose dependencies are not exported with it,
# and for a shared library that includes the private ones.
set(VKM_EXPORTED_DEPS glm glm-header-only glfw vkm_log nlohmann_json glew stb miniaudio vkm_build_info vkm_warnings)
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
# ships in the SDK rather than being rebuilt per game.
install(TARGETS vkm_runtime_app vkm_editor_app vkm_cook_app vkm_server_app
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)

# src/engine ships and nothing else does: a project writes behaviors and builds
# scenes, and reaches neither the GL backend nor the editor. The directory
# structure is the include path, so it is preserved exactly - "ecs/scene.h"
# resolves the same against the SDK as it does in the engine tree.
#
# build_info.h is the one exception. It prints the APP_* macros the build
# injects into the hosts, and those deliberately do not travel in a project's
# compile interface - so the header would ship as something no consumer can
# compile. It belongs to the hosts, and only they include it.
install(DIRECTORY ${CMAKE_SOURCE_DIR}/src/engine/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        COMPONENT Development
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
        PATTERN "build_info.h" EXCLUDE
)

# The third-party headers the engine's own public headers include - the complete
# set, so a project compiles against the SDK without vendoring anything itself.
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
#
# Three directories into include/tracy/, not tracy/ alone into include/, because
# tracy/Tracy.hpp reaches sideways - "../common/TracyColor.hpp",
# "../client/TracyProfiler.hpp" - and those resolve against the directory it was
# found in. Landing it flat in include/ puts that parent one level too high, and
# the first consumer to include debug/profiler.h stops at a missing TracyColor.
# This layout is the one TracyClient's own INSTALL_INTERFACE names, so the
# imported target's include directory and the files agree.
if(VKM_PROFILER)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/modules/tracy/public/tracy
                      ${CMAKE_SOURCE_DIR}/modules/tracy/public/client
                      ${CMAKE_SOURCE_DIR}/modules/tracy/public/common
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tracy COMPONENT Development
            FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
endif()

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

install(EXPORT vkmEngineTargets
        FILE        vkmEngineTargets.cmake
        NAMESPACE   vkmEngine::
        DESTINATION ${VKM_CMAKE_INSTALL_DIR}
        COMPONENT   Development)

# The build tree is a package too. `tools/vkm` says it finds the engine there so
# the tool is usable before anything is installed, and that only works if a
# complete package - config, version file and targets file - sits where
# find_package looks. It lands under the same lib/cmake/vkmEngine the install
# uses, so one path answers for both layouts.
set(VKM_BUILD_TREE_PACKAGE_DIR ${CMAKE_BINARY_DIR}/${VKM_CMAKE_INSTALL_DIR})

export(EXPORT vkmEngineTargets
       FILE      ${VKM_BUILD_TREE_PACKAGE_DIR}/vkmEngineTargets.cmake
       NAMESPACE vkmEngine::)

# The gameplay module recipe, which the config file includes from beside itself.
# Copied rather than generated: it holds no configure-time substitutions, and
# copying it means the top-level CMakeLists and both package layouts read one
# file. COPYONLY, so a `$`-bearing line is never mistaken for a substitution.
configure_file(${CMAKE_SOURCE_DIR}/cmake/gameplay_module.cmake
               ${VKM_BUILD_TREE_PACKAGE_DIR}/gameplay_module.cmake COPYONLY)

# The two layouts the config file is generated for. Installed, every path hangs
# off the prefix the SDK was unpacked into and has to stay relocatable, so the
# strings below are escaped and resolved when the config is loaded. In the build
# tree the binaries are under build/ while the headers, shaders and templates
# never left the source tree, so those are absolute and settled here.
set(VKM_PKG_ROOT_DIR     "\${PACKAGE_PREFIX_DIR}")
set(VKM_PKG_BIN_DIR      "\${PACKAGE_PREFIX_DIR}/${CMAKE_INSTALL_BINDIR}")
set(VKM_PKG_INCLUDE_DIR  "\${PACKAGE_PREFIX_DIR}/${CMAKE_INSTALL_INCLUDEDIR}")
set(VKM_PKG_SHADER_DIR   "\${PACKAGE_PREFIX_DIR}/shaders")
set(VKM_PKG_TEMPLATE_DIR "\${PACKAGE_PREFIX_DIR}/templates")
configure_package_config_file(
    ${CMAKE_SOURCE_DIR}/cmake/vkmEngineConfig.cmake.in
    ${CMAKE_BINARY_DIR}/install/vkmEngineConfig.cmake
    INSTALL_DESTINATION ${VKM_CMAKE_INSTALL_DIR}
)

set(VKM_PKG_ROOT_DIR     "${CMAKE_SOURCE_DIR}")
set(VKM_PKG_BIN_DIR      "${CMAKE_BINARY_DIR}/bin")
set(VKM_PKG_INCLUDE_DIR  "${CMAKE_SOURCE_DIR}/src/engine")
set(VKM_PKG_SHADER_DIR   "${CMAKE_SOURCE_DIR}/shaders")
set(VKM_PKG_TEMPLATE_DIR "${CMAKE_SOURCE_DIR}/templates")
configure_package_config_file(
    ${CMAKE_SOURCE_DIR}/cmake/vkmEngineConfig.cmake.in
    ${VKM_BUILD_TREE_PACKAGE_DIR}/vkmEngineConfig.cmake
    INSTALL_DESTINATION ${VKM_CMAKE_INSTALL_DIR}
)

# SameMinorVersion: the engine is not ABI-stable across minor releases, but a
# project asking for 1.4 should accept 1.4.2. The toolchain pin in the config
# file is what catches the case this cannot. One file serves both layouts - it
# names a version, not a path - so it is written into the build-tree package and
# installed from there.
write_basic_package_version_file(
    ${VKM_BUILD_TREE_PACKAGE_DIR}/vkmEngineConfigVersion.cmake
    VERSION       ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion
)

install(FILES
            ${CMAKE_BINARY_DIR}/install/vkmEngineConfig.cmake
            ${VKM_BUILD_TREE_PACKAGE_DIR}/vkmEngineConfigVersion.cmake
            ${CMAKE_SOURCE_DIR}/cmake/gameplay_module.cmake
        DESTINATION ${VKM_CMAKE_INSTALL_DIR}
        COMPONENT   Development)
