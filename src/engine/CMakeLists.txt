# EngineCore - ECS, resources, IO, platform, and the non-render systems.
# CONFIGURE_DEPENDS re-globs on demand, so adding/removing a source file does
# not need a manual reconfigure.
file(GLOB_RECURSE ENGINECORE_SOURCES CONFIGURE_DEPENDS
    core/*.cpp
    ecs/*.cpp
    io/*.cpp
    resource/*.cpp
    system/animation/*.cpp
    system/particle/*.cpp
    system/async/*.cpp
    system/camera/*.cpp
    system/visibility/*.cpp
    system/physics/*.cpp
    system/script/*.cpp
    system/sky/*.cpp
    system/event/*.cpp
    system/hierarchy/*.cpp
    system/ui/*.cpp
    system/io/*.cpp
    platform/input/*.cpp
    platform/window/*.cpp
    platform/threading/*.cpp
    platform/library/*.cpp
    debug/*.cpp
)

# Shared, not static, and that is the SDK's central decision. A gameplay module
# has to reach engine symbols without carrying a second copy of them: two copies
# mean two typeId registries and two sets of singletons, so a component type
# registered against one is invisible to the other. A static engine can only
# satisfy that by having the module resolve symbols from the host executable,
# which works on Linux but binds a module to one specific exe on Windows - so a
# module built for the editor cannot be loaded by the runtime. One shared library
# both link against removes the choice of host from the question.
add_library(EngineCore SHARED ${ENGINECORE_SOURCES})

# The public surface is not yet annotated, so on Windows let CMake generate the
# export table from the object files. Narrowing this to an explicit VKM_API is a
# later step of the same version; it changes what is exported, not whether the
# shared build works.
set_target_properties(EngineCore PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

# The engine include root. Split so the same "ecs/scene.h" resolves against the
# source tree during development and against <prefix>/include from an SDK.
target_include_directories(EngineCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# GLEW headers for window_manager.cpp's glewInit (glew uses old-style include dirs).
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
    BuildInfo          # io/project_paths.h resolves paths against APP_ROOT_DIR
    stb                # platform/window/window_manager.cpp decodes the window icon
)

target_compile_definitions(EngineCore PUBLIC
    GLM_ENABLE_EXPERIMENTAL
    GLM_FORCE_INTRINSICS

    # The engine version, visible to anything that links EngineCore - including a
    # gameplay module built against an installed SDK, which does not link
    # BuildInfo. A module reports this back through vkmModuleEngineVersion() and
    # the host compares it with its own, so a module built against a different
    # engine is refused at load with a sentence instead of crashing later.
    VKM_ENGINE_VERSION="${PROJECT_VERSION}"
)

# Tracy plumbing. The facade (debug/profiler.h) gates every macro on
# VKM_PROFILER, so consumers never include Tracy headers themselves. TracyClient
# stays a PUBLIC link so backend/editor TUs that pull profile macros via the
# facade get the symbols at link time.
if(VKM_PROFILER)
    target_compile_definitions(EngineCore PUBLIC VKM_PROFILER=1)
    target_link_libraries     (EngineCore PUBLIC TracyClient)
endif()

# EngineHeaders - an include-only (INTERFACE) view of EngineCore's public API.
# Kept for consumers that genuinely need headers without a link, but a gameplay
# module is no longer one of them: it links EngineCore directly now that
# EngineCore is shared, which gives it the one typeId registry and the one set of
# singletons rather than resolving them from whichever exe loaded it.
# The GLM_* defs are mandatory either way: glm types cross the module boundary
# (e.g. CollisionEvent), so both sides must agree on glm's layout.
add_library(EngineHeaders INTERFACE)
target_include_directories(EngineHeaders INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
    $<TARGET_PROPERTY:glm,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:glfw,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:vkmLog,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:nlohmann_json,INTERFACE_INCLUDE_DIRECTORIES>
)
target_compile_definitions(EngineHeaders INTERFACE
    GLM_ENABLE_EXPERIMENTAL
    GLM_FORCE_INTRINSICS
)
if(VKM_PROFILER)
    target_compile_definitions(EngineHeaders INTERFACE VKM_PROFILER=1)
endif()

# EngineRendering - render system, backend-neutral interface, and render view.
file(GLOB_RECURSE ENGINERENDERING_SOURCES CONFIGURE_DEPENDS system/render/*.cpp)

add_library(EngineRendering SHARED ${ENGINERENDERING_SOURCES})

set_target_properties(EngineRendering PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

target_include_directories(EngineRendering PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(EngineRendering
    PUBLIC  EngineCore
    PRIVATE vkm_warnings
)
