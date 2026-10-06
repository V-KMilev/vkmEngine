# vkm_core - ECS, resources, IO, platform, and the non-render systems.
# Everything under src/engine except the render system, which is its own shared
# library below. Enumerated as one sweep and one removal rather than as a list of
# directories: a list has to be edited to add a subsystem, and the edit is
# invisible when it is forgotten - the new .cpp simply does not link.
file(GLOB_RECURSE VKM_CORE_SOURCES CONFIGURE_DEPENDS *.cpp)
file(GLOB_RECURSE VKM_RENDER_SOURCES CONFIGURE_DEPENDS system/render/*.cpp)
list(REMOVE_ITEM VKM_CORE_SOURCES ${VKM_RENDER_SOURCES})

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
    stb                # image decode and encode
    miniaudio          # system/audio owns the device
)

# The headers those private dependencies own are still part of this library's
# public surface - platform/window/glfw_include.h names GLFW types - so consumers
# need the include paths without the archives. SYSTEM, like the module's own
# (modules/CMakeLists.txt): they are not the engine's to warn about.
target_include_directories(vkm_core SYSTEM PUBLIC
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

# No fused multiply-add unless the source writes one. GCC contracts a * b + c
# into one instruction by default wherever the target has it, and a fused
# result rounds once where the written one rounds twice - so the same tick on a
# machine built for FMA and one built without lands somewhere different, and
# replay and the wire both rest on it landing in the same place. Public, because
# gameplay predicted on both ends of a session compiles the same inline math
# into its own module.
target_compile_options(vkm_core PUBLIC
    $<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>
)

# Asserts on (VKM_ASSERTS, CMakeLists.txt), publicly, so a module agrees.
if(VKM_ASSERTS)
    target_compile_options(vkm_core PUBLIC $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)
endif()

# A module built against a shipping engine keeps its debug info too.
if(VKM_SHIPPING)
    target_compile_options(vkm_core PUBLIC $<$<CXX_COMPILER_ID:GNU,Clang>:-g>)
endif()

# Winsock, for platform/net. It is not linked by anything else the engine
# already pulls in - GLFW has no use for it - so without this the socket calls
# resolve to nothing at link time. Private, like the other absorbed libraries:
# the sockets live inside libvkm_core and nothing outside names a winsock type.
# winmm is the same case for FrameLimiter's timeBeginPeriod.
if(WIN32)
    target_link_libraries(vkm_core PRIVATE ws2_32 winmm)
endif()

# Tracy is linked privately for the same reason as glfw: its objects live inside
# libvkm_core, and a consumer expanding a PROFILE_ macro resolves the symbols
# from there. Its include path and its compile definitions still propagate,
# because the facade's macros expand into Tracy's header in the consumer's own
# translation unit, and that header compiles every zone to nothing unless
# TRACY_ENABLE is defined where it is included. The definitions are the
# client's whole option set (TRACY_ON_DEMAND among them) rather than the one
# flag, so a consumer's zone has the same layout as the client that receives it.
if(VKM_PROFILER)
    target_compile_definitions(vkm_core PUBLIC
        VKM_PROFILER=1
        $<TARGET_PROPERTY:TracyClient,INTERFACE_COMPILE_DEFINITIONS>
    )
    target_link_libraries(vkm_core PRIVATE TracyClient)
    target_include_directories(vkm_core SYSTEM PUBLIC
        $<TARGET_PROPERTY:TracyClient,INTERFACE_INCLUDE_DIRECTORIES>
    )
endif()

# vkm_render - render system, backend-neutral interface, and render view.
# Its sources are globbed above, where they are also removed from vkm_core's.
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
