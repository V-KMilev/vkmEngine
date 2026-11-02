# vkm_pipeline - the asset pipeline: the importers that read source art (cgltf and ufbx, stb LDR
# decode, vkmAudio's audio decode) in import/, the cooker that bakes recipes into the cooked cache in
# cook/, and the mesh operations both use in mesh/ (simplification and reordering with
# meshoptimizer, the lightmap unwrap). Never linked by the runtime, so a shipped game carries no
# importer it cannot use.
file(GLOB_RECURSE VKM_PIPELINE_SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(vkm_pipeline STATIC ${VKM_PIPELINE_SOURCES})

target_include_directories(vkm_pipeline PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# The BC4/BC5 encoder is the cook's inner loop beside stb_image_resize and bc7e,
# and is optimized in a Debug build for the reason they are (modules/CMakeLists.txt).
if(NOT MSVC)
    set_source_files_properties(cook/texture/bc4_encoder.cpp
        PROPERTIES COMPILE_OPTIONS $<$<CONFIG:Debug>:-O2>
    )
endif()

target_link_libraries(vkm_pipeline PUBLIC
    vkm_core
    vkm_loader         # the image decoders
)
target_link_libraries(vkm_pipeline PRIVATE
    cgltf              # glTF and GLB import
    ufbx               # FBX and OBJ import
    mikktspace         # tangents for an imported mesh that carries none
    meshoptimizer      # LOD simplification, draw order and the import's vertex weld
    xatlas             # lightmap UVs
    stb                # the LDR decode
    stb_image_resize   # mip chains
    bc7e               # BC7 encode
    vkm_warnings
)
