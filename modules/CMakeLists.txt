# Shared dependencies the ENGINE owns, added before vkmOpenGL, which requires glm and
# vkmLog as targets (modules/vkmOpenGL/modules/CMakeLists.txt). glm is in behavior-facing
# engine headers and the image decoder is engine-wide, so neither can sit behind
# a backend: a second one would either vendor its own glm and risk a version
# skew across the module boundary, or reach through the GL backend for it.
#
# Vendored dependencies install themselves by default, which would scatter their
# headers, archives and pkgconfig files through the SDK prefix. The engine picks
# what ships (cmake/install.cmake), so each is switched off at its own option.
#
# Third-party subprojects are added SYSTEM: their headers reach first-party
# code as system headers, so the warnings the engine holds itself to - Clang's
# -Wdocumentation among them - are not applied to code the engine does not own.
# vkmLog, vkmPlatform, vkmAudio, vkmNetwork, vkmPhysics and vkmOpenGL are first-party and are not.
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
if(NOT TARGET glm)
    add_subdirectory(glm SYSTEM)
    # On glm itself, so every target that compiles its inline math - the engine, vkmOpenGL,
    # the tests - compiles the same code: a vector op built with intrinsics in one
    # target and without in another may round differently.
    target_compile_definitions(glm PUBLIC
        GLM_ENABLE_EXPERIMENTAL
        GLM_FORCE_INTRINSICS
    )
endif()

# Each library's suite runs in the engine's ctest, as the engine's own tests do: every tree builds it but
# the shipping engine's, which builds no test.
if(VKM_SHIPPING)
    set(_vkm_library_tests OFF)
else()
    set(_vkm_library_tests ON)
endif()

set(VKM_LOG_BUILD_TESTS ${_vkm_library_tests} CACHE BOOL "" FORCE)
if(NOT TARGET vkm_log)
    add_subdirectory(vkmLog)
endif()

# Tracy, only when VKM_PROFILER is on. On demand and on localhost alone, so a game
# that carries it costs nothing until a profiler connects and opens no port to
# the network; profiling from another machine needs TRACY_ONLY_LOCALHOST off.
if(VKM_PROFILER)
    set(TRACY_ENABLE         ON  CACHE BOOL "" FORCE)
    set(TRACY_ON_DEMAND      ON  CACHE BOOL "" FORCE)
    set(TRACY_ONLY_LOCALHOST ON  CACHE BOOL "" FORCE)
    set(TRACY_NO_BROADCAST   OFF CACHE BOOL "" FORCE)
    set(TRACY_NO_FRAME_IMAGE ON  CACHE BOOL "" FORCE)
    # Built because vkm_core links it; its own install rules are left out
    # (cmake/install.cmake installs what a project needs of it).
    if(NOT TARGET TracyClient)
        add_subdirectory(tracy SYSTEM EXCLUDE_FROM_ALL)
        # Two results Tracy leaves unread (a pipe() in TracyProfiler.cpp, an fscanf() in
        # TracySysPower.cpp), which GCC warns of whatever SYSTEM says; its source stays as written.
        target_compile_options(TracyClient PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-unused-result>)
    endif()
endif()

# vkmPlatform: the window, its input, the thread pool and processes, static and linked whole
# into vkm_core (src/engine/CMakeLists.txt). Its debug features follow the engine's own, so
# Tracy is added above it.
#
# It adds GLFW, shared, because vkm_core (through vkmPlatform) and the imgui target below
# both link it. A static glfw would be absorbed into libvkm_core and, since the shared
# engine re-exports what it absorbs, a target linking both would get glfw defined twice -
# once through the engine's import library and once through the archive imgui links.
# Windows makes that a link error; Linux merely hides it.
set(GLFW_LIBRARY_TYPE SHARED CACHE STRING "" FORCE)
set(VKM_PLATFORM_ASSERTS  ${VKM_ASSERTS}  CACHE BOOL "" FORCE)
set(VKM_PLATFORM_PROFILER ${VKM_PROFILER} CACHE BOOL "" FORCE)
set(VKM_PLATFORM_BUILD_TESTS ${_vkm_library_tests} CACHE BOOL "" FORCE)
if(NOT TARGET vkm_platform)
    add_subdirectory(vkmPlatform)
endif()

# vkmAudio: the device, the mixer, the acoustics and the reverb, static and linked whole into
# vkm_core (src/engine/CMakeLists.txt). Its asserts follow the engine's own.
set(VKM_AUDIO_ASSERTS ${VKM_ASSERTS} CACHE BOOL "" FORCE)
set(VKM_AUDIO_BUILD_TESTS ${_vkm_library_tests} CACHE BOOL "" FORCE)
if(NOT TARGET vkm_audio)
    add_subdirectory(vkmAudio)
endif()

# vkmNetwork: the network's transport, static and linked whole into vkm_core
# (src/engine/CMakeLists.txt). Its asserts follow the engine's own.
set(VKM_NETWORK_ASSERTS ${VKM_ASSERTS} CACHE BOOL "" FORCE)
set(VKM_NETWORK_BUILD_TESTS ${_vkm_library_tests} CACHE BOOL "" FORCE)
if(NOT TARGET vkm_network)
    add_subdirectory(vkmNetwork)
endif()

# vkmPhysics: the rigid-body physics, static and linked whole into vkm_core
# (src/engine/CMakeLists.txt). Its debug features follow the engine's own, so Tracy
# is added above it.
set(VKM_PHYSICS_ASSERTS  ${VKM_ASSERTS}  CACHE BOOL "" FORCE)
set(VKM_PHYSICS_PROFILER ${VKM_PROFILER} CACHE BOOL "" FORCE)
set(VKM_PHYSICS_BUILD_TESTS ${_vkm_library_tests} CACHE BOOL "" FORCE)
if(NOT TARGET vkm_physics)
    add_subdirectory(vkmPhysics)
endif()

# stb is header-only upstream. The implementation is compiled once into a tiny
# static lib so any engine target, not just the GL backend, can decode an image.
if(NOT TARGET stb)
    set(_stb_impl ${CMAKE_CURRENT_BINARY_DIR}/stb_image_impl.c)
    file(WRITE ${_stb_impl} "#define STB_IMAGE_IMPLEMENTATION\n#include \"stb_image.h\"\n")
    add_library(stb STATIC ${_stb_impl})
    target_include_directories(stb SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/stb)
    # GCC 15.2's jump threading miscompiles the jump into stbi__hdr_load's flat loop: a flat-stored
    # .hdr at least 8 texels wide decodes its first row alone (tests/loader/hdr.cpp). Its source as written.
    target_compile_options(stb PRIVATE $<$<C_COMPILER_ID:GNU>:-fno-thread-jumps>)
endif()

# stb's PNG writer and font rasterizer, compiled the same way. The writer is the
# tests' (the engine's screenshot keeps a private copy, its settings its own);
# the rasterizer and its rect packer are the font baker's (src/loader/font_baker.h).
if(NOT TARGET stb_image_write)
    set(_stbiw_impl ${CMAKE_CURRENT_BINARY_DIR}/stb_image_write_impl.c)
    file(WRITE ${_stbiw_impl} "#define STB_IMAGE_WRITE_IMPLEMENTATION\n#include \"stb_image_write.h\"\n")
    add_library(stb_image_write STATIC ${_stbiw_impl})
    target_include_directories(stb_image_write SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/stb)
endif()

if(NOT TARGET stb_truetype)
    set(_stbtt_impl ${CMAKE_CURRENT_BINARY_DIR}/stb_truetype_impl.c)
    file(WRITE ${_stbtt_impl}
        "#define STB_RECT_PACK_IMPLEMENTATION\n#include \"stb_rect_pack.h\"\n"
        "#define STB_TRUETYPE_IMPLEMENTATION\n#include \"stb_truetype.h\"\n"
    )
    add_library(stb_truetype STATIC ${_stbtt_impl})
    target_include_directories(stb_truetype SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/stb)
endif()

# The texture cook's two halves, each compiled once into a static lib the
# runtime does not link: a shipped game uploads what was cooked and never
# filters or encodes a texture itself. stb_image_resize2 builds the mip chains,
# its implementation written out the way stb_image's is above. BC7 blocks come from
# bc7e, the scalar encoder of basis_universal: its two self-contained files, as they
# are at that project's commit 99f52d63, under their Apache 2.0 license and notice.
# BC4 and BC5 are the engine's own (src/pipeline/cook/texture/bc4_encoder.h).
#
# Both are optimized in a Debug build too. They are the cook's inner loops, and
# unoptimized they turn a save that bakes a few textures into minutes; nobody
# steps through them.
if(NOT TARGET stb_image_resize)
    set(_stbir_impl ${CMAKE_CURRENT_BINARY_DIR}/stb_image_resize_impl.c)
    file(WRITE ${_stbir_impl} "#define STB_IMAGE_RESIZE_IMPLEMENTATION\n#include \"stb_image_resize2.h\"\n")
    add_library(stb_image_resize STATIC ${_stbir_impl})
    target_include_directories(stb_image_resize SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/stb)
endif()

if(NOT TARGET bc7e)
    add_library(bc7e STATIC bc7e/basisu_bc7e_scalar.cpp)
    target_include_directories(bc7e SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/bc7e)
endif()

if(NOT MSVC)
    target_compile_options(stb_image_resize PRIVATE $<$<CONFIG:Debug>:-O2>)
    target_compile_options(bc7e             PRIVATE $<$<CONFIG:Debug>:-O2>)
endif()

# Model import: cgltf reads glTF and GLB, ufbx reads FBX and OBJ, and the
# MikkTSpace reference implementation builds tangents. It is the one bakers bake
# normal maps against, and a tangent frame built any other way shades a baked
# map subtly wrong. All three are C, compiled once into static libs the runtime
# does not link. cgltf is one header, so its implementation is written out the
# way stb_image's is above.
if(NOT TARGET cgltf)
    set(_cgltf_impl ${CMAKE_CURRENT_BINARY_DIR}/cgltf_impl.c)
    file(WRITE ${_cgltf_impl} "#define CGLTF_IMPLEMENTATION\n#include \"cgltf.h\"\n")
    add_library(cgltf STATIC ${_cgltf_impl})
    target_include_directories(cgltf SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cgltf)
endif()

if(NOT TARGET ufbx)
    add_library(ufbx STATIC ufbx/ufbx.c)
    target_include_directories(ufbx SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/ufbx)
endif()

if(NOT TARGET mikktspace)
    add_library(mikktspace STATIC mikktspace/mikktspace.c)
    target_include_directories(mikktspace SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/mikktspace)
endif()

# vkmOpenGL: the OpenGL layer. Adds glew, and defers to the targets above. Its debug
# features follow the engine's own: its asserts with VKM_ASSERTS, and the error check
# after every GL call in a Debug build alone, as each check is a round trip to the
# driver a dev build should not pay - by configuration, as a multi-config generator
# leaves CMAKE_BUILD_TYPE empty. Its test suite is off unless a consumer asks, so it
# is asked for explicitly.
set(VKM_OPENGL_ASSERTS ${VKM_ASSERTS} CACHE BOOL "" FORCE)
set(VKM_OPENGL_CHECKS "$<CONFIG:Debug>" CACHE STRING "" FORCE)
set(VKM_OPENGL_BUILD_TESTS ${_vkm_library_tests} CACHE BOOL "" FORCE)
if(NOT TARGET vkm_opengl)
    add_subdirectory(vkmOpenGL)
endif()

# nlohmann/json (single-header, INTERFACE target `nlohmann_json`). Its own
# install rules are off because the engine installs the subset it wants through
# its own EXPORT set.
set(JSON_Install   OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
if(NOT TARGET nlohmann_json)
    add_subdirectory(json SYSTEM)
endif()

# meshoptimizer: LOD simplification, vertex-cache and vertex-fetch ordering, and
# the import's vertex weld. A cook-time dependency like the importers - the
# runtime does not link it - so it builds static and installs nothing.
set(MESHOPT_INSTALL OFF CACHE BOOL "" FORCE)
if(NOT TARGET meshoptimizer)
    add_subdirectory(meshoptimizer SYSTEM)
endif()

# xatlas: the lightmap UVs a baked mesh is unwrapped into. A cook-time dependency like
# meshoptimizer, and optimized in a Debug build for the reason the cook's encoders are:
# an unwrap is a long inner loop nobody steps through. Single-threaded: its own pool, started
# inside each of the cook's workers, put a few dozen threads on a few cores. Its trigonometry and its
# one qsort are the engine's (src/pipeline/mesh/xatlas_portable.h) and no multiply-add is fused, so an
# unwrap is the same on every platform; its source as written.
if(NOT TARGET xatlas)
    set(_xatlas_unit ${CMAKE_CURRENT_BINARY_DIR}/xatlas_unit.cpp)
    file(CONFIGURE OUTPUT ${_xatlas_unit} CONTENT "#include \"mesh/xatlas_portable.h\"\n#include \"xatlas.cpp\"\n")
    add_library(xatlas STATIC ${_xatlas_unit})
    target_include_directories(xatlas SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/xatlas/source/xatlas)
    target_include_directories(xatlas PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/pipeline)
    target_compile_definitions(xatlas PRIVATE XA_MULTITHREADED=0)
    target_compile_options(xatlas PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>)
    if(NOT MSVC)
        target_compile_options(xatlas PRIVATE $<$<CONFIG:Debug>:-O2>)
    endif()
endif()

# imgui_stdlib.cpp: upstream InputText over std::string.
add_library(imgui STATIC
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/misc/cpp/imgui_stdlib.cpp
    imgui/backends/imgui_impl_glfw.cpp
)

target_include_directories(imgui SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
)

target_link_libraries(imgui PUBLIC glfw)

# ImGui's OpenGL renderer, apart so only the editor's host links it. It loads GL
# through its own loader, not GLEW.
add_library(imgui_opengl3 STATIC imgui/backends/imgui_impl_opengl3.cpp)
target_link_libraries(imgui_opengl3 PUBLIC imgui)

# Every module's license, a file each in the build's licenses/, which the SDK installs and `vkm package`
# ships in every game: each is built into what the engine ships, and its terms travel with it. Beside
# them the one shader the engine ports from another's code, and the engine's own: one file, as its
# libraries' repositories hold the same terms byte for byte (tests/docs/check_docs.py).
set(VKM_LICENSES_DIR ${CMAKE_BINARY_DIR}/licenses)
set(_licenses "")
foreach(_license
    "bc7e=modules/bc7e/LICENSE"
    "cgltf=modules/cgltf/LICENSE"
    "fsr1=shaders/opengl/post/upscale/LICENSE-MIT"
    "glew=modules/vkmOpenGL/modules/glew/LICENSE.txt"
    "glfw=modules/vkmPlatform/modules/glfw/LICENSE.md"
    "glm=modules/glm/copying.txt"
    "imgui=modules/imgui/LICENSE.txt"
    "meshoptimizer=modules/meshoptimizer/LICENSE.md"
    "miniaudio=modules/vkmAudio/modules/miniaudio/LICENSE"
    "nlohmann_json=modules/json/LICENSE.MIT"
    "stb=modules/stb/LICENSE"
    "tracy=modules/tracy/LICENSE"
    "ufbx=modules/ufbx/LICENSE"
    "vkmEngine=LICENSE"
    "xatlas=modules/xatlas/LICENSE"
)
    string(REPLACE "=" ";" _license ${_license})
    list(GET _license 0 _name)
    list(GET _license 1 _file)
    configure_file(${CMAKE_SOURCE_DIR}/${_file} ${VKM_LICENSES_DIR}/${_name}.txt COPYONLY)
    list(APPEND _licenses ${VKM_LICENSES_DIR}/${_name}.txt)
endforeach()
# mikktspace's terms are its header's opening comment.
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/mikktspace/mikktspace.h _mikktspace LIMIT 2000)
string(REGEX MATCH "Copyright[^/]*" _mikktspace "${_mikktspace}")
string(REGEX REPLACE "\n \\*  ?" "\n" _mikktspace "${_mikktspace}")
string(REGEX REPLACE "\n \\*$" "" _mikktspace "${_mikktspace}")
file(CONFIGURE OUTPUT ${VKM_LICENSES_DIR}/mikktspace.txt CONTENT "${_mikktspace}\n" @ONLY)
list(APPEND _licenses ${VKM_LICENSES_DIR}/mikktspace.txt)
# A file the list no longer names goes, so a build tree older than the list ships no license of what it
# does not hold.
file(GLOB _present ${VKM_LICENSES_DIR}/*)
list(REMOVE_ITEM _present ${_licenses})
if(_present)
    file(REMOVE ${_present})
endif()
