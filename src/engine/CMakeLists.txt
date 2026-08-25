# vkm_core - ECS, resources, IO, platform, and the non-render systems.
file(GLOB_RECURSE VKM_CORE_SOURCES CONFIGURE_DEPENDS
    core/*.cpp
    ecs/*.cpp
    io/*.cpp
    resource/*.cpp
    system/animation/*.cpp
    system/audio/*.cpp
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

# Shared, not static: a gameplay module has to reach engine symbols without
# carrying a second copy of them, and one shared library both it and the host
# link against is the only arrangement that does not bind a module to one exe.
# docs/reference/building.md carries the rest of the reasoning.
add_library(vkm_core SHARED ${VKM_CORE_SOURCES})

# The public surface is not annotated, so on Windows CMake generates the export
# table from the object files instead.
set_target_properties(vkm_core PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

# The engine include root. Split so the same "ecs/scene.h" resolves against the
# source tree during development and against <prefix>/include from an SDK.
target_include_directories(vkm_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# GLEW headers for window_manager.cpp's glewInit (glew uses old-style include dirs).
target_include_directories(vkm_core PRIVATE
    ${PROJECT_SOURCE_DIR}/modules/vkmGL/modules/glew/include
)

# glm, vkm_log and nlohmann are header-only or shared, so a consumer linking them
# too costs nothing and resolves to one copy.
target_link_libraries(vkm_core PUBLIC
    glm
    vkm_log
    nlohmann_json
)

# Static libraries absorbed INTO this shared library are linked privately, and
# only their include directories travel outward (below). A consumer must not link
# them again: their code is already inside libvkm_core, and on Windows - where
# the export table re-publishes everything the DLL absorbed - linking both the
# import library and the static archive is a duplicate-definition error at link
# time. Linux hides this because the dynamic linker picks one; Windows does not.
target_link_libraries(vkm_core PRIVATE
    glfw
    glew
    vkm_warnings
    vkm_build_info     # io/project_paths.h resolves paths against APP_ROOT_DIR
    stb                # platform/window/window_manager.cpp decodes the window icon
    miniaudio          # system/audio owns the device; nothing else sees the header
)

# The headers those private dependencies own are still part of this library's
# public surface - platform/window/glfw_include.h names GLFW types - so consumers
# need the include paths without the archives.
target_include_directories(vkm_core PUBLIC
    $<TARGET_PROPERTY:glfw,INTERFACE_INCLUDE_DIRECTORIES>
)

target_compile_definitions(vkm_core PUBLIC
    GLM_ENABLE_EXPERIMENTAL
    GLM_FORCE_INTRINSICS

    # Here rather than in vkm_build_info, which a gameplay module built against
    # an installed SDK does not link: the module reports this back through
    # vkmModuleEngineVersion(), and a host that reads a different number refuses
    # the module at load instead of crashing later.
    VKM_ENGINE_VERSION="${PROJECT_VERSION}"
)

# Tracy is linked privately for the same reason as glfw: its objects live inside
# libvkm_core, and a consumer expanding a PROFILE_ macro resolves the symbols
# from there. Its include path still propagates, because the facade's macros
# expand into Tracy's header in the consumer's own translation unit.
if(VKM_PROFILER)
    target_compile_definitions(vkm_core PUBLIC VKM_PROFILER=1)
    target_link_libraries     (vkm_core PRIVATE TracyClient)
    target_include_directories(vkm_core PUBLIC
        $<TARGET_PROPERTY:TracyClient,INTERFACE_INCLUDE_DIRECTORIES>
    )
endif()

# vkm_headers - an include-only (INTERFACE) view of vkm_core's public API, for a
# consumer that needs the headers without the link. A gameplay module is not one
# of them; it links vkm_core. The GLM_* definitions are not optional here: glm
# types cross the module boundary (CollisionEvent, for one), so both sides have
# to agree on glm's layout.
add_library(vkm_headers INTERFACE)
target_include_directories(vkm_headers INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
    $<TARGET_PROPERTY:glm,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:glfw,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:vkm_log,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:nlohmann_json,INTERFACE_INCLUDE_DIRECTORIES>
)
target_compile_definitions(vkm_headers INTERFACE
    GLM_ENABLE_EXPERIMENTAL
    GLM_FORCE_INTRINSICS
)
if(VKM_PROFILER)
    target_compile_definitions(vkm_headers INTERFACE VKM_PROFILER=1)
endif()

# vkm_render - render system, backend-neutral interface, and render view.
file(GLOB_RECURSE VKM_RENDER_SOURCES CONFIGURE_DEPENDS system/render/*.cpp)

add_library(vkm_render SHARED ${VKM_RENDER_SOURCES})

set_target_properties(vkm_render PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

target_include_directories(vkm_render PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(vkm_render
    PUBLIC  vkm_core
    PRIVATE vkm_warnings
)
