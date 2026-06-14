# EngineCore - ECS, resources, IO, platform, and the non-render systems.
# CONFIGURE_DEPENDS re-globs on demand, so adding/removing a source file does
# not need a manual reconfigure.
file(GLOB_RECURSE ENGINECORE_SOURCES CONFIGURE_DEPENDS
    core/*.cpp
    ecs/*.cpp
    io/*.cpp
    resource/*.cpp
    system/animation/*.cpp
    system/async/*.cpp
    system/camera/*.cpp
    system/visibility/*.cpp
    system/physics/*.cpp
    system/event/*.cpp
    system/hierarchy/*.cpp
    system/io/*.cpp
    platform/window/*.cpp
    platform/threading/*.cpp
    debug/*.cpp
)

add_library(EngineCore STATIC ${ENGINECORE_SOURCES})

target_include_directories(EngineCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# GLEW headers for window.cpp's glewInit (glew uses old-style include dirs).
target_include_directories(EngineCore PRIVATE
    ${PROJECT_SOURCE_DIR}/modules/vkmGL/modules/glew/include
)

target_link_libraries(EngineCore PUBLIC
    glm
    glfw
    vkmLog
    nlohmann_json
)
target_link_libraries(EngineCore PRIVATE
    glew
    vkm_warnings
    BuildInfo          # io/screenshot.cpp resolves paths against APP_ROOT_DIR
)

target_compile_definitions(EngineCore PUBLIC
    GLM_ENABLE_EXPERIMENTAL
    GLM_FORCE_INTRINSICS
)

# Tracy plumbing. The facade (debug/profiler.h) gates every macro on
# VKM_PROFILER, so consumers never include Tracy headers themselves. TracyClient
# stays a PUBLIC link so backend/editor TUs that pull profile macros via the
# facade get the symbols at link time.
if(VKM_PROFILER)
    target_compile_definitions(EngineCore PUBLIC VKM_PROFILER=1)
    target_link_libraries     (EngineCore PUBLIC TracyClient)
endif()

# EngineRendering - render system, backend-neutral interface, and render view.
file(GLOB_RECURSE ENGINERENDERING_SOURCES CONFIGURE_DEPENDS system/render/*.cpp)

add_library(EngineRendering STATIC ${ENGINERENDERING_SOURCES})

target_include_directories(EngineRendering PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(EngineRendering
    PUBLIC  EngineCore
    PRIVATE vkm_warnings
)
