# vkm_tools - runtime-safe asset tools: procedural generators, shader +
# environment loaders, and the cooked-asset factory registration. Links stb
# (HDR float decode for environments) but NOT Assimp - the heavy importers live
# in vkm_cook, so a runtime build never pulls Assimp.
file(GLOB_RECURSE VKM_TOOLS_SOURCES CONFIGURE_DEPENDS *.cpp)

# The heavy importers (Assimp / stb LDR decode / miniaudio decode) and the asset
# cooker compile into vkm_cook (editor-only) instead. Their headers stay here on
# the shared vkm_tools include path, so editor TUs keep including "loader/...".
set(VKM_COOK_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/loader/audio_loaders.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/loader/model_loaders.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/loader/texture_loaders.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/loader/material_loaders.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cook/recipe_registration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cook/asset_cooker.cpp
)
list(REMOVE_ITEM VKM_TOOLS_SOURCES ${VKM_COOK_SOURCES})

add_library(vkm_tools STATIC ${VKM_TOOLS_SOURCES})

target_include_directories(vkm_tools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# stb_image (header-only) for the HDR float decode in loader/environment_loaders.cpp.
# The stb implementation is provided by the `stb` module, linked transitively via
# vkm_core, so no explicit stb link is needed here.
target_include_directories(vkm_tools PRIVATE
    ${PROJECT_SOURCE_DIR}/modules/stb
)

target_link_libraries(vkm_tools PUBLIC
    vkm_core
)
target_link_libraries(vkm_tools PRIVATE
    vkm_warnings
    vkm_build_info     # resolves relative asset paths against APP_ROOT_DIR
)

# vkm_cook - the heavy importers (Assimp, stb LDR decode, miniaudio audio
# decode) and the asset cooker that bakes recipes into the cooked cache. Linked
# by the editor and by vkm_cook_app; never by the runtime, which is what keeps a
# shipped game free of an importer it cannot use - the link list guarantees
# that, not a build flag.
add_library(vkm_cook STATIC ${VKM_COOK_SOURCES})

# stb_image for the LDR decode, miniaudio for the audio decode. Both
# implementations are already inside vkm_core; only the declarations are needed.
target_include_directories(vkm_cook PRIVATE
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
    vkm_tools          # generators + shared headers on the include path
)
target_link_libraries(vkm_cook PRIVATE
    assimp             # model import, used by loader/model_loaders.cpp
    vkm_warnings
    vkm_build_info
)
