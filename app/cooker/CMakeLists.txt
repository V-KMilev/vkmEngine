# engine_cook - bakes a project's assets with no window and no GL context, so a
# release build needs no GUI step and no GPU. Links the tools (Assimp, image
# decode) the runtime deliberately excludes.
add_executable(engine_cook main.cpp)

target_link_libraries(engine_cook PRIVATE
    EngineCore
    EngineTools
    EngineCooker
    BuildInfo
    vkm_warnings
)

target_include_directories(engine_cook PRIVATE ${CMAKE_SOURCE_DIR})

install(TARGETS engine_cook RUNTIME DESTINATION bin COMPONENT Tools)
