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
if(NOT TARGET glm)
    add_subdirectory(glm)
endif()

set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
if(NOT TARGET glfw)
    add_subdirectory(glfw)
endif()

if(NOT TARGET vkmLog)
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

# vkmGL: the OpenGL layer. Adds glew, and defers to the targets above.
if(NOT TARGET vkmGL)
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
