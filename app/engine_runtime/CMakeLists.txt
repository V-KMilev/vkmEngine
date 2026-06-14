# engine_runtime - bare engine executable, no editor. Foundation for a future
# game binary.
add_executable(engine_runtime main.cpp)

target_link_libraries(engine_runtime PRIVATE
    EngineApp
    BuildInfo
    vkm_warnings
)
