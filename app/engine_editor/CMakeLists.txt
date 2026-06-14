# engine_editor - engine + in-process editor (ImGui panels, gizmos, scene I/O
# dialogs). Built only when VKM_WITH_EDITOR=ON.
add_executable(engine_editor main.cpp)

target_link_libraries(engine_editor PRIVATE
    EngineApp
    EngineEditor
    BuildInfo
    vkm_warnings
)

# Where the editor looks for the hot-reloadable game module at runtime (the
# unified output dir, next to the exe).
target_compile_definitions(engine_editor PRIVATE
    GAME_MODULE_DIR="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

# Export the exe's symbols so the hot-reloadable game module can resolve engine
# symbols against it (the module is loaded at runtime, never linked into the exe).
# Windows: generates an import library (libengine_editor.dll.a) the module links;
# --export-all-symbols publishes the statically-linked engine symbols too.
# Linux/macOS: -rdynamic/--export-dynamic puts the exe's symbols in the dynamic
# table so the dlopen'd module resolves against the host (no link needed there).
set_target_properties(engine_editor PROPERTIES ENABLE_EXPORTS ON)
if(WIN32)
    target_link_options(engine_editor PRIVATE "-Wl,--export-all-symbols")
else()
    target_link_options(engine_editor PRIVATE "-rdynamic")
endif()

# The game module: gameplay behaviors compiled as a shared library the editor
# dlopen's for hot-reload. Compiles against EngineHeaders (include-only) and
# resolves engine symbols from this exe.
file(GLOB_RECURSE GAME_MODULE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/game/*.cpp)
add_library(game_module SHARED ${GAME_MODULE_SOURCES})
target_include_directories(game_module PRIVATE ${CMAKE_SOURCE_DIR}/game)
target_link_libraries(game_module PRIVATE EngineHeaders vkm_warnings)
if(WIN32)
    target_link_libraries(game_module PRIVATE engine_editor)
endif()
# Output as game.dll / libgame.so / libgame.dylib (matches DynamicLibrary::platformName).
set_target_properties(game_module PROPERTIES OUTPUT_NAME game)
if(WIN32)
    set_target_properties(game_module PROPERTIES PREFIX "")
endif()
