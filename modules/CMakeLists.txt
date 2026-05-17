# ============================================================================
# External Dependencies
# ============================================================================
if(NOT TARGET vkmLog)
    add_subdirectory(vkmLog)
endif()

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
)

target_include_directories(imgui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/imgui>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/imgui>
)

target_link_libraries(imgui PUBLIC
    glfw
)

# imgui uses its own built-in GL loader (imgui_impl_opengl3_loader.h)
# -- no GLEW dependency, no loader defines needed
