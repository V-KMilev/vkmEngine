# engine_runtime - bare engine executable, no editor. Foundation for a future
# game binary.
add_executable(engine_runtime main.cpp)

# The app drives the engine directly, so it links the engine libs itself.
target_link_libraries(engine_runtime PRIVATE
    EngineCore
    EngineRendering
    EngineTools
    BackendOpenGL
    BuildInfo
    vkm_warnings
)

# main.cpp includes the shared bootstrap as "app/engine_app.h"; the repo root is
# on the include path so that prefix resolves.
target_include_directories(engine_runtime PRIVATE ${CMAKE_SOURCE_DIR})

# The gameplay module is loaded, not linked. It resolves engine symbols from the
# shared engine libraries it links itself, so this executable exports nothing and
# a module is not bound to whichever host opened it.
