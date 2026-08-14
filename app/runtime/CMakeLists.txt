# engine_runtime - bare engine executable, no editor. Foundation for a future
# game binary.
add_executable(engine_runtime main.cpp)

# The app drives the engine directly (no EngineApp library), so it links the
# engine libs - the same set EngineApp used to re-export.
target_link_libraries(engine_runtime PRIVATE
    EngineCore
    EngineRendering
    EngineTools
    BackendOpenGL
    BuildInfo
    vkm_warnings
)

# main.cpp includes the shared bootstrap as "app/engine_app.h" (which pulls in
# "example/default_scene.h"); the repo root puts both prefixes on the include path.
target_include_directories(engine_runtime PRIVATE ${CMAKE_SOURCE_DIR})

# The gameplay module is loaded, not linked - the same module the editor
# dlopen's, so a game is data plus a shared library rather than a rebuilt
# engine. Where to find it, and the symbol export it resolves against:
# --export-dynamic puts this exe's symbols in the dynamic table so the module
# binds to the host that loaded it, exactly as it does under the editor.
set_target_properties(engine_runtime PROPERTIES ENABLE_EXPORTS ON)
if(WIN32)
    target_link_options(engine_runtime PRIVATE "-Wl,--export-all-symbols")
else()
    target_link_options(engine_runtime PRIVATE "-rdynamic")
endif()

# ----------------------------------------------------------------------------
# Packaging: assemble a relocatable game under the install prefix.
#
# The exe + its DLLs go in bin/; the data sits at the package root. The runtime
# resolves its root from the executable's directory (see project_paths.cpp),
# checking both that dir and its parent, so exe-in-bin/ with data one level up
# runs standalone. Everything below is tagged COMPONENT Runtime so the package
# step can exclude the third-party submodules' own install rules (headers, static
# libs); package with:
#     cmake --install build --prefix dist --component Runtime
# yielding:  dist/bin/{exe,*.dll}  +  dist/{shaders,scenes,cooked,library,assets}
#
# Prerequisite: cook the assets first - `engine_cook <project>` - so library/ +
# cooked/ are populated; the runtime can neither import nor cook. That step
# needs no window and no GPU, so packaging runs unattended. Data
# ships the way the runtime consumes it: shaders + HDR envs as SOURCE (compiled
# / IBL-baked at load), meshes/textures as COOKED binaries, materials as their
# inline library recipes. The runtime creates logs/ on startup, so it isn't shipped.

# Everything in bin/ links against its siblings there - the exe against its .so
# deps, and those against each other (vkmGL -> vkmLog, glew). $ORIGIN points the
# loader at each binary's own directory, so the package runs once moved off the
# build machine. CMake links with the build-tree rpath and rewrites it to this on
# install; Windows resolves DLLs from the exe's directory and needs no rpath.
if(NOT WIN32)
    set_target_properties(engine_runtime vkmGL vkmLog glew
        PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()

install(TARGETS engine_runtime RUNTIME DESTINATION bin COMPONENT Runtime)

# Shared libraries the exe links (glew, vkmGL, vkmLog; glfw is static). On Windows
# these are DLLs gathered from the linked imports; that genex is empty elsewhere,
# so on other platforms install the library targets directly - install() carries
# the version symlink chain (libGLEW.so -> .so.2.3 -> .so.2.3.0) the loader needs.
install(FILES $<TARGET_RUNTIME_DLLS:engine_runtime> DESTINATION bin COMPONENT Runtime OPTIONAL)
if(NOT WIN32)
    install(TARGETS vkmGL vkmLog glew LIBRARY DESTINATION bin COMPONENT Runtime)
endif()

# The compiler's own runtime DLLs are not imported targets, so copy the MinGW
# trio from the toolchain bin when present (UCRT / Win10+ ships ucrtbase itself).
if(MINGW)
    get_filename_component(_vkm_mingw_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(_dll libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
        if(EXISTS "${_vkm_mingw_bin}/${_dll}")
            install(FILES "${_vkm_mingw_bin}/${_dll}" DESTINATION bin COMPONENT Runtime)
        endif()
    endforeach()
endif()

# Always-shipped source data.
install(DIRECTORY ${CMAKE_SOURCE_DIR}/shaders DESTINATION . COMPONENT Runtime)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/scenes  DESTINATION . COMPONENT Runtime)

# Cooked mesh/texture binaries - the form the runtime consumes them in.
install(DIRECTORY ${CMAKE_SOURCE_DIR}/cooked DESTINATION . COMPONENT Runtime OPTIONAL)

# Library: ship only the runtime-relevant subset. The runtime reads the manifest
# (resolves asset names -> cooked files + recipe hashes) and the material recipes
# (materials are not cooked - they are rebuilt from these inline recipes at load).
# library/meshes and library/textures hold the source recipes the editor re-cooks
# from; the runtime never reads them. OPTIONAL so the install works before a cook.
install(FILES     ${CMAKE_SOURCE_DIR}/library/_manifest.json DESTINATION library COMPONENT Runtime OPTIONAL)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/library/materials      DESTINATION library COMPONENT Runtime OPTIONAL)

# HDR environments (IBL is baked from these at load) and the window icon
# (setupEngineApp loads it on startup). assets/models is import-source only - the
# runtime uses the cooked meshes, so it is not shipped.
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/envs  DESTINATION assets COMPONENT Runtime OPTIONAL)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/logo  DESTINATION assets COMPONENT Runtime OPTIONAL)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/fonts DESTINATION assets COMPONENT Runtime OPTIONAL)
