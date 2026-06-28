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
    game
    BuildInfo
    vkm_warnings
)

# main.cpp includes the shared bootstrap as "app/engine_app.h" (which pulls in
# "example/default_scene.h"); the repo root puts both prefixes on the include path.
target_include_directories(engine_runtime PRIVATE ${CMAKE_SOURCE_DIR})

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
# Prerequisite: COOK the assets first (open engine_editor and save once) so
# library/ + cooked/ are populated - the runtime cannot import or cook. Data
# ships the way the runtime consumes it: shaders + HDR envs as SOURCE (compiled
# / IBL-baked at load), meshes/textures as COOKED binaries, materials as their
# inline library recipes. The runtime creates logs/ on startup, so it isn't shipped.
install(TARGETS engine_runtime RUNTIME DESTINATION bin COMPONENT Runtime)

# Shared libraries the exe links (glew, vkmGL, vkmLog here; glfw is static).
install(FILES $<TARGET_RUNTIME_DLLS:engine_runtime> DESTINATION bin COMPONENT Runtime OPTIONAL)

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

# Cooked output + library recipes (manifest + inline materials). OPTIONAL so the
# install doesn't fail before the first cook - but the game needs them at runtime.
install(DIRECTORY ${CMAKE_SOURCE_DIR}/cooked  DESTINATION . COMPONENT Runtime OPTIONAL)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/library DESTINATION . COMPONENT Runtime OPTIONAL)

# HDR environments (IBL is baked from these at load).
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/envs DESTINATION assets COMPONENT Runtime OPTIONAL)
