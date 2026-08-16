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

target_include_directories(engine_runtime PRIVATE ${CMAKE_SOURCE_DIR})
