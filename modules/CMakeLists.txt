# ============================================================================
# External Dependencies
# ============================================================================
# Shared dependencies the ENGINE owns, added before vkmGL so its own
# `if (TARGET ...)` guards defer to these.
#
# They used to be vkmGL's, and the engine reached through the GL backend to get
# them - glm is in fifteen behaviour-facing engine headers, and the image
# decoder was included from vkmGL/modules/stb. That works with one backend and
# breaks with two: a second backend would either vendor its own glm and risk a
# version skew across the module boundary, or depend on the GL backend to get
# one. They belong beside the engine that needs them.
#
# vkmGL keeps glew, which is the only genuinely OpenGL-specific one. It expects
# the rest as targets a parent provides, and hard-errors when they are absent.
# Vendored dependencies install themselves by default, which would scatter their
# headers, archives and pkgconfig files through the SDK prefix. The engine picks
# what ships (cmake/install.cmake); each of these is switched off at its own
# option so nothing lands that we did not choose.
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
if(NOT TARGET glm)
    add_subdirectory(glm)
endif()

set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)

# Shared, because two different things call into glfw: vkm_core owns the window,
# and ImGui's GLFW backend inside the editor drives input. A static glfw would be
# absorbed into libvkm_core and, since the shared engine re-exports what it
# absorbs, the editor would end up with glfw defined twice - once through the
# engine's import library and once through the archive ImGui links. Windows makes
# that a link error; Linux merely hides it. One shared glfw means one copy, and
# nothing has to know which of its callers got there first.
set(GLFW_LIBRARY_TYPE   SHARED CACHE STRING "" FORCE)
if(NOT TARGET glfw)
    add_subdirectory(glfw)
endif()

if(NOT TARGET vkm_log)
    add_subdirectory(vkmLog)
endif()

# ============================================================================
# stb (image decoding). Header-only upstream; we compile the implementation
# once into a tiny static lib so any engine target (not just the GL backend)
# can decode images - the window icon is the first user. The headers are the
# engine's own stb submodule now; vkmGL compiles a file-local copy
# (STB_IMAGE_STATIC), so there is no duplicate-symbol clash.
# ============================================================================
if(NOT TARGET stb)
    set(_stb_impl ${CMAKE_CURRENT_BINARY_DIR}/stb_image_impl.c)
    file(WRITE ${_stb_impl} "#define STB_IMAGE_IMPLEMENTATION\n#include \"stb_image.h\"\n")
    add_library(stb STATIC ${_stb_impl})
    # Split for export: stb travels in the engine's link interface, so its
    # include path has to resolve against an installed SDK as well as this tree.
    target_include_directories(stb PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/stb>
        $<INSTALL_INTERFACE:include>
    )
endif()

# ============================================================================
# miniaudio (audio device + wav/mp3/flac decoding). Public domain, one header,
# and the one-line implementation translation unit it ships beside it - so the
# implementation is compiled once here instead of being written out the way
# stb's is above. The engine absorbs it privately: only src/engine/system/audio
# and the audio importer include the header, and nothing that plays a sound
# needs to know it exists.
#
# Three subsystems are compiled out rather than left unused. Encoding and
# waveform/noise generation have no caller. The resource manager is the one
# that matters: it is miniaudio's own file loader, and it would both spawn a
# job thread the engine never asked for and give the audio engine a second way
# to acquire a sound - one that bypasses ResourceManager, the asset library and
# the cooked cache. Removing it makes that seam a link error rather than a rule.
# ============================================================================
if(NOT TARGET miniaudio)
    add_library(miniaudio STATIC miniaudio/miniaudio.c)
    # Split for export, like stb: miniaudio travels in the engine's link
    # interface, so its include path has to resolve against an installed SDK too.
    target_include_directories(miniaudio PUBLIC
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

# vkmGL: the OpenGL layer. Adds glew, and defers to the targets above.
#
# Its test suite defaults to "on only when vkm_gl is the top-level project",
# which as a submodule here means never - so the one suite in the tree had never
# been compiled. Asked for explicitly instead: it covers what runs without a GL
# context (vertex layout arithmetic, the shader preprocessor), so it costs one
# small translation unit and passes on a machine with no GPU.
set(VKM_GL_BUILD_TESTS ON CACHE BOOL "" FORCE)
if(NOT TARGET vkm_gl)
    add_subdirectory(vkmGL)
endif()

# nlohmann/json (single-header, INTERFACE target `nlohmann_json`).
# Suppress its own install rules and tests — the parent project installs
# whatever subset of targets it wants via its own EXPORT set.
set(JSON_Install   OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
if(NOT TARGET nlohmann_json)
    add_subdirectory(json)
endif()


# ============================================================================
# Assimp (model import: glTF / OBJ / FBX / DAE / STL / PLY / 3DS).
# Trimmed hard: no tools/tests/samples/exporters, only the importers we use,
# and the bundled zlib so the build stays self-contained on MSYS2/clang.
# ============================================================================
set(ASSIMP_BUILD_TESTS                    OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS             OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES                  OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL                        OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL_PDB                    OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT                      ON  CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER            ON  CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER             ON  CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER             ON  CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_COLLADA_IMPORTER         ON  CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_STL_IMPORTER             ON  CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_PLY_IMPORTER             ON  CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_3DS_IMPORTER             ON  CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB                     ON  CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_DRACO                    OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS             OFF CACHE BOOL "" FORCE)
if(NOT TARGET assimp)
    set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(assimp)
endif()

# ============================================================================
# FreeType (font rasterizer for ImGui). Vendored so the build doesn't depend
# on a system libfreetype being installed. Trimmed to the core rasterizer:
# no zlib/bzip2/png/harfbuzz/brotli - none of which UI text needs - keeping
# the build self-contained like assimp above.
# ============================================================================
set(FT_DISABLE_ZLIB     ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2    ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG      ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI   ON CACHE BOOL "" FORCE)
if(NOT TARGET freetype)
    set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
    add_subdirectory(freetype)
endif()

# ============================================================================
# ImGui Library
# ============================================================================
add_library(imgui STATIC
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_demo.cpp
    imgui/misc/cpp/imgui_stdlib.cpp   # upstream InputText over std::string
    imgui/backends/imgui_impl_glfw.cpp
    imgui/backends/imgui_impl_opengl3.cpp
    imgui/misc/freetype/imgui_freetype.cpp
)

target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
)

# FreeType font rasterizer: proper hinting, visibly crisper than the default
# stb_truetype path at UI sizes (which has no hinting at all). Vendored above.
target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_FREETYPE)
target_link_libraries(imgui PUBLIC
    glfw
    freetype
)

# imgui uses its own built-in GL loader (imgui_impl_opengl3_loader.h)
# -- no GLEW dependency, no loader defines needed

# ============================================================================
# Tracy (CPU + GPU profiler). Gated by VKM_PROFILER so release builds compile
# Tracy with TRACY_ENABLE=OFF, making every client macro a zero-cost no-op.
# Engine code talks to Tracy only through debug/profiler.h.
# ============================================================================
if(VKM_PROFILER)
    set(TRACY_ENABLE         ON  CACHE BOOL "" FORCE)
    set(TRACY_ON_DEMAND      ON  CACHE BOOL "" FORCE)
    set(TRACY_NO_BROADCAST   OFF CACHE BOOL "" FORCE)
    set(TRACY_NO_FRAME_IMAGE ON  CACHE BOOL "" FORCE)
else()
    set(TRACY_ENABLE OFF CACHE BOOL "" FORCE)
endif()
if(NOT TARGET TracyClient)
    add_subdirectory(tracy)
endif()
