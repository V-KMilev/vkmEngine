# engine_editor - engine + in-process editor (ImGui panels, gizmos, scene I/O
# dialogs). Built only when VKM_WITH_EDITOR=ON.
add_executable(engine_editor main.cpp)

# The app drives the engine directly (no EngineApp library), so it links the
# engine libs - the same set EngineApp used to re-export - plus EngineEditor.
target_link_libraries(engine_editor PRIVATE
    EngineCore
    EngineRendering
    EngineTools
    EngineCooker
    BackendOpenGL
    EngineEditor
    BuildInfo
    vkm_warnings
)

# main.cpp includes the shared bootstrap as "app/engine_app.h" (which pulls in
# "example/default_scene.h"); the repo root puts both prefixes on the include path.
target_include_directories(engine_editor PRIVATE ${CMAKE_SOURCE_DIR})

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


