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
