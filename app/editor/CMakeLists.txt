# engine_editor - engine + in-process editor (ImGui panels, gizmos, scene I/O
# dialogs).
add_executable(engine_editor main.cpp)

# The app drives the engine directly, so it links the engine libs itself.
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

# main.cpp includes the shared bootstrap as "app/engine_app.h"; the repo root is
# on the include path so that prefix resolves.
target_include_directories(engine_editor PRIVATE ${CMAKE_SOURCE_DIR})

# The gameplay module is loaded, not linked. It resolves engine symbols from the
# shared engine libraries it links itself, so this executable exports nothing and
# a module is not bound to whichever host opened it.


