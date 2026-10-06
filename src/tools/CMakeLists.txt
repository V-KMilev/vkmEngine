# vkm_tools - the runtime-safe tools: the HDR environment and plain image
# decoders, the SDF font baker and the host prologue. The heavy importers live in
# vkm_cook, so a runtime build pulls no model importer.
file(GLOB_RECURSE VKM_TOOLS_SOURCES CONFIGURE_DEPENDS *.cpp)

# Which target a file builds into is the folder it sits in: import/ and cook/
# are vkm_cook, everything else here is vkm_tools. Their headers stay on the
# shared vkm_tools include path, so editor TUs include "import/..." directly.
file(GLOB VKM_COOK_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/import/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cook/*.cpp
)
list(REMOVE_ITEM VKM_TOOLS_SOURCES ${VKM_COOK_SOURCES})

add_library(vkm_tools STATIC ${VKM_TOOLS_SOURCES})

target_include_directories(vkm_tools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# stb_image's declarations, for the HDR and plain image decodes under loader/. The
# implementation is compiled once, into vkm_core, which exports what it absorbs
# (src/engine/CMakeLists.txt), so nothing here links it again.
target_include_directories(vkm_tools SYSTEM PRIVATE
    ${PROJECT_SOURCE_DIR}/modules/stb
)

target_link_libraries(vkm_tools PUBLIC
    vkm_core
)
target_link_libraries(vkm_tools PRIVATE
    vkm_warnings
    vkm_build_info     # printBuildInfo
)

# vkm_cook - the heavy importers (cgltf and ufbx, stb LDR decode, miniaudio audio
# decode), the asset cooker that bakes recipes into the cooked cache, and what
# it bakes with: mesh simplification and reordering (meshoptimizer), the mip
# filter and the block encoder its textures go through. Never linked by the
# runtime, so a shipped game carries no importer it cannot use.
add_library(vkm_cook STATIC ${VKM_COOK_SOURCES})

# The BC4/BC5 encoder is the cook's inner loop beside stb_image_resize and bc7e,
# and is optimised in a Debug build for the reason they are (modules/CMakeLists.txt).
if(NOT MSVC)
    set_source_files_properties(cook/bc4_encoder.cpp PROPERTIES COMPILE_OPTIONS $<$<CONFIG:Debug>:-O2>)
endif()

# stb_image for the LDR decode, miniaudio for the audio decode. Both
# implementations are already inside vkm_core; only the declarations are needed.
target_include_directories(vkm_cook SYSTEM PRIVATE
    ${PROJECT_SOURCE_DIR}/modules/stb
    ${PROJECT_SOURCE_DIR}/modules/miniaudio
)

# Borrowing miniaudio's header means borrowing the definitions it was compiled
# with: MA_NO_RESOURCE_MANAGER and friends change the layout of structs the
# header declares and the implementation defines, and neither the compiler nor
# the linker diagnoses a mismatch. Taken from the target rather than restated,
# so the two configurations cannot drift - and without linking it, so the
# implementation stays single-copy inside libvkm_core. stb needs no such line;
# it has no macro that moves a field.
target_compile_definitions(vkm_cook PRIVATE
    $<TARGET_PROPERTY:miniaudio,INTERFACE_COMPILE_DEFINITIONS>
)

target_link_libraries(vkm_cook PUBLIC
    vkm_core
    vkm_tools          # the image decoders, and the shared include path
)
target_link_libraries(vkm_cook PRIVATE
    cgltf              # glTF and GLB import
    ufbx               # FBX and OBJ import
    mikktspace         # tangents for an imported mesh that carries none
    meshoptimizer      # LOD simplification, draw order and the import's vertex weld
    stb_image_resize   # mip chains
    bc7e               # BC7 encode
    vkm_warnings
    vkm_build_info
)
