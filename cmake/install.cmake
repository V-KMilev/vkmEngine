# The SDK install, every rule of it in this file. An SDK is what games are built
# with; packaging a game is `vkm package`. The layout is in
# docs/reference/building.md.

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(VKM_CMAKE_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/vkmEngine)

# A project links vkm_core alone. Its dependencies are exported with it because
# CMake will not export a target without them, private ones included.
set(VKM_EXPORTED_DEPS
    glm
    glm-header-only
    glfw
    vkm_log
    nlohmann_json
    glew
    stb
    miniaudio
    vkm_build_info
    vkm_warnings
)
if(VKM_PROFILER)
    list(APPEND VKM_EXPORTED_DEPS TracyClient)
endif()

# Shared libraries go in bin/ beside the hosts, where Windows and the rpath look.
install(TARGETS vkm_core ${VKM_EXPORTED_DEPS}
    EXPORT  vkmEngineTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# Shared libraries the hosts load but a project never links: they ship as files,
# not as imported targets.
install(TARGETS vkm_render vkm_gl
    LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# The hosts; a shipping engine has only the two a game runs.
set(VKM_HOSTS vkm_runtime_app vkm_server_app)
if(NOT VKM_SHIPPING)
    list(APPEND VKM_HOSTS vkm_editor_app vkm_cook_app)
endif()
install(TARGETS ${VKM_HOSTS}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# The shipping engine an SDK carries for `vkm package`: a VKM_SHIPPING build tree
# of this source, installed whole into <prefix>/shipping.
set(VKM_SHIPPING_ENGINE "" CACHE PATH "A VKM_SHIPPING build tree whose engine the SDK carries")
if(VKM_SHIPPING_ENGINE AND NOT VKM_SHIPPING)
    install(CODE "
        execute_process(
            COMMAND \"${CMAKE_COMMAND}\" --install \"${VKM_SHIPPING_ENGINE}\"
                --prefix \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/shipping\"
            RESULT_VARIABLE _vkm_shipping_installed
        )
        if(_vkm_shipping_installed)
            message(FATAL_ERROR \"The shipping engine in ${VKM_SHIPPING_ENGINE} did not install\")
        endif()
    ")
endif()

# src/engine's headers alone, laid out as the include path ("ecs/scene.h"). Not
# build_info.h, whose macros vkm_core keeps private, so no consumer compiles it.
install(DIRECTORY ${CMAKE_SOURCE_DIR}/src/engine/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
    PATTERN "build_info.h" EXCLUDE
)

# The third-party headers the engine's own public headers include - the complete
# set, so a project compiles against the SDK without vendoring anything itself.
install(DIRECTORY ${CMAKE_SOURCE_DIR}/modules/glm/glm
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h" PATTERN "*.inl"
)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/modules/json/single_include/nlohmann
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/modules/glfw/include/GLFW
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# No GL headers: nothing a project can reach names an OpenGL type. The GPU
# profiler header includes glew, and it belongs to the backend, not src/engine.
install(FILES
    ${CMAKE_SOURCE_DIR}/modules/vkmLog/src/logger.h
    ${CMAKE_SOURCE_DIR}/modules/vkmLog/src/l_assert.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Tracy's headers, when the engine profiles. Three directories under
# include/tracy/, as TracyClient's INSTALL_INTERFACE names them: Tracy.hpp
# includes "../common/..." and "../client/..." from beside itself.
if(VKM_PROFILER)
    install(DIRECTORY
        ${CMAKE_SOURCE_DIR}/modules/tracy/public/tracy
        ${CMAKE_SOURCE_DIR}/modules/tracy/public/client
        ${CMAKE_SOURCE_DIR}/modules/tracy/public/common
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tracy
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
    )
endif()

# Shaders are engine chrome: they ship with the engine and a project never edits
# them. Everything a project owns - its scenes, its assets, its cooked library -
# belongs to the project and is not the SDK's to install.
install(DIRECTORY ${CMAKE_SOURCE_DIR}/shaders DESTINATION .)

# The engine's font and logo, less the logo's design sources (as LOGO_SOURCES in
# tools/vkm leaves them out of a package).
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/fonts ${CMAKE_SOURCE_DIR}/assets/logo
    DESTINATION assets
    OPTIONAL
    PATTERN "_preview" EXCLUDE
    PATTERN "source_split" EXCLUDE
)

# What `vkm new` copies to make a project.
install(DIRECTORY ${CMAKE_SOURCE_DIR}/templates DESTINATION .)

# The compiler's runtime DLLs (VKM_COMPILER_RUNTIME, CMakeLists.txt).
install(FILES ${VKM_COMPILER_RUNTIME} DESTINATION ${CMAKE_INSTALL_BINDIR})

# The project tool, and on Windows the shim cmd.exe and PowerShell run it by.
install(PROGRAMS ${CMAKE_SOURCE_DIR}/tools/vkm DESTINATION ${CMAKE_INSTALL_BINDIR})
if(WIN32)
    install(PROGRAMS ${CMAKE_SOURCE_DIR}/tools/vkm.cmd DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(EXPORT vkmEngineTargets
    FILE        vkmEngineTargets.cmake
    NAMESPACE   vkmEngine::
    DESTINATION ${VKM_CMAKE_INSTALL_DIR}
)

# The build tree is a complete package too, under the same lib/cmake/vkmEngine
# as an install, so a project builds against the engine before any install.
set(VKM_BUILD_TREE_PACKAGE_DIR ${CMAKE_BINARY_DIR}/${VKM_CMAKE_INSTALL_DIR})

export(EXPORT vkmEngineTargets
    FILE      ${VKM_BUILD_TREE_PACKAGE_DIR}/vkmEngineTargets.cmake
    NAMESPACE vkmEngine::
)

# Written into the build-tree package and installed from there: neither names a path.
configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/vkmEngineConfig.cmake.in
    ${VKM_BUILD_TREE_PACKAGE_DIR}/vkmEngineConfig.cmake
    @ONLY
)
configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/gameplay_module.cmake
    ${VKM_BUILD_TREE_PACKAGE_DIR}/gameplay_module.cmake
    COPYONLY
)

# For a project that names a version to find_package; project.json's is checked
# by vkm_check_engine_version.
write_basic_package_version_file(
    ${VKM_BUILD_TREE_PACKAGE_DIR}/vkmEngineConfigVersion.cmake
    VERSION       ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion
)

install(FILES
    ${VKM_BUILD_TREE_PACKAGE_DIR}/vkmEngineConfig.cmake
    ${VKM_BUILD_TREE_PACKAGE_DIR}/vkmEngineConfigVersion.cmake
    ${VKM_BUILD_TREE_PACKAGE_DIR}/gameplay_module.cmake
    DESTINATION ${VKM_CMAKE_INSTALL_DIR}
)
