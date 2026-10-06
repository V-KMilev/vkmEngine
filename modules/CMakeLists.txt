# Shared dependencies the ENGINE owns, added before vkmGL, which requires them
# as targets (modules/vkmGL/modules/CMakeLists.txt). glm is in behaviour-facing
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
# vkmLog and vkmGL are first-party and are not.
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
if(NOT TARGET glm)
    add_subdirectory(glm SYSTEM)
endif()

set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)

# Shared, because vkm_core and the imgui target below both link glfw. A static
# glfw would be absorbed into libvkm_core and, since the shared engine re-exports
# what it absorbs, a target linking both would get glfw defined twice - once
# through the engine's import library and once through the archive imgui links.
# Windows makes that a link error; Linux merely hides it.
set(GLFW_LIBRARY_TYPE SHARED CACHE STRING "" FORCE)
if(NOT TARGET glfw)
    add_subdirectory(glfw SYSTEM)
endif()

if(NOT TARGET vkm_log)
    add_subdirectory(vkmLog)
endif()

# stb is header-only upstream. The implementation is compiled once into a tiny
# static lib so any engine target, not just the GL backend, can decode an image.
if(NOT TARGET stb)
    set(_stb_impl ${CMAKE_CURRENT_BINARY_DIR}/stb_image_impl.c)
    file(WRITE ${_stb_impl} "#define STB_IMAGE_IMPLEMENTATION\n#include \"stb_image.h\"\n")
    add_library(stb STATIC ${_stb_impl})
    # Split for export: stb travels in the engine's link interface, so its
    # include path has to resolve against an installed SDK as well as this tree.
    target_include_directories(stb SYSTEM PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/stb>
        $<INSTALL_INTERFACE:include>
    )
endif()

# The texture cook's two halves, each compiled once into a static lib the
# runtime does not link: a shipped game uploads what was cooked and never
# filters or encodes a texture itself. stb_image_resize2 builds the mip chains,
# its implementation written out the way stb_image's is above. BC7 blocks come from
# the scalar bc7e of basis_universal, the one file of that submodule compiled;
# BC4 and BC5 are the engine's own (src/tools/cook/).
#
# Both are optimised in a Debug build too. They are the cook's inner loops, and
# unoptimised they turn a save that bakes a few textures into minutes; nobody
# steps through them.
if(NOT TARGET stb_image_resize)
    set(_stbir_impl ${CMAKE_CURRENT_BINARY_DIR}/stb_image_resize_impl.c)
    file(WRITE ${_stbir_impl} "#define STB_IMAGE_RESIZE_IMPLEMENTATION\n#include \"stb_image_resize2.h\"\n")
    add_library(stb_image_resize STATIC ${_stbir_impl})
    target_include_directories(stb_image_resize SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/stb)
endif()

if(NOT TARGET bc7e)
    add_library(bc7e STATIC basis_universal/encoder/basisu_bc7e_scalar.cpp)
    target_include_directories(bc7e SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/basis_universal/encoder)
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

# miniaudio (audio device + wav/mp3/flac decoding) ships an implementation
# translation unit of its own, so unlike stb above nothing has to be written out
# here.
#
# Three subsystems are compiled out rather than left unused. The resource
# manager is the one that matters: it is miniaudio's own file loader, and it
# would both spawn a job thread the engine never asked for and give the audio
# engine a second way to acquire a sound - one that bypasses ResourceManager,
# the asset library and the cooked cache. Removing it makes that seam a link
# error rather than a rule.
if(NOT TARGET miniaudio)
    add_library(miniaudio STATIC miniaudio/miniaudio.c)
    # Split for export, like stb: miniaudio travels in the engine's link
    # interface, so its include path has to resolve against an installed SDK too.
    target_include_directories(miniaudio SYSTEM PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/miniaudio>
        $<INSTALL_INTERFACE:include>
    )
    # PUBLIC because the header reads them as well: a consumer that compiled
    # with a different set would see a different ma_engine_config layout.
    target_compile_definitions(miniaudio PUBLIC
        MA_NO_ENCODING
        MA_NO_GENERATION
        MA_NO_RESOURCE_MANAGER
    )
    # The Linux backends (ALSA / PulseAudio / JACK) are dlopen'ed at runtime, so
    # miniaudio links none of them - it needs the loader and a thread library.
    find_package(Threads REQUIRED)
    target_link_libraries(miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()

# vkmGL: the OpenGL layer. Adds glew, and defers to the targets above. Its test
# suite defaults to "on only when vkm_gl is the top-level project", which as a
# submodule here means never, so it is asked for explicitly.
set(VKM_GL_BUILD_TESTS ON CACHE BOOL "" FORCE)
if(NOT TARGET vkm_gl)
    add_subdirectory(vkmGL)
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

# FreeType (font rasterizer for ImGui), vendored so the build does not depend on
# a system libfreetype. Trimmed to the core rasterizer - UI text needs neither
# the compression codecs nor complex script shaping.
set(FT_DISABLE_ZLIB     ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2    ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG      ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI   ON CACHE BOOL "" FORCE)
if(NOT TARGET freetype)
    set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
    add_subdirectory(freetype SYSTEM)
endif()

# imgui_stdlib.cpp: upstream InputText over std::string.
add_library(imgui STATIC
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_demo.cpp
    imgui/misc/cpp/imgui_stdlib.cpp
    imgui/backends/imgui_impl_glfw.cpp
    imgui/backends/imgui_impl_opengl3.cpp
    imgui/misc/freetype/imgui_freetype.cpp
)

target_include_directories(imgui SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
)

# FreeType font rasterizer: proper hinting, visibly crisper than the default
# stb_truetype path at UI sizes (which has no hinting at all).
target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_FREETYPE)
target_link_libraries(imgui PUBLIC
    glfw
    freetype
)

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
    endif()
endif()
