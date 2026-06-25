# EngineTools - runtime-safe asset tools: procedural generators, shader +
# environment loaders, and the cooked-asset factory registration. Links stb
# (HDR float decode for environments) but NOT Assimp - the heavy importers live
# in EngineCooker, so a runtime build never pulls Assimp.
file(GLOB_RECURSE ENGINETOOLS_SOURCES CONFIGURE_DEPENDS *.cpp)

# The heavy importers (Assimp / stb LDR decode) and the asset cooker compile into
# EngineCooker (editor-only) instead. Their headers stay here on the shared
# EngineTools include path, so editor TUs keep including "loader/...".
set(ENGINECOOKER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/loader/model_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/loader/texture_loaders.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/loader/material_loaders.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cook/recipe_registration.cpp
)
list(REMOVE_ITEM ENGINETOOLS_SOURCES ${ENGINECOOKER_SOURCES})

add_library(EngineTools STATIC ${ENGINETOOLS_SOURCES})

target_include_directories(EngineTools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# stb_image (header-only) for the HDR float decode in loader/environment_loaders.cpp.
# The stb implementation is provided by the `stb` module, linked transitively via
# EngineCore, so no explicit stb link is needed here.
target_include_directories(EngineTools PRIVATE
    ${PROJECT_SOURCE_DIR}/modules/vkmGL/modules/stb
)

target_link_libraries(EngineTools PUBLIC
    EngineCore
)
target_link_libraries(EngineTools PRIVATE
    vkm_warnings
    BuildInfo          # resolves relative asset paths against APP_ROOT_DIR
)

# EngineCooker - editor-only heavy importers (Assimp + stb LDR decode) and the
# asset cooker that bakes recipes into the cooked cache. Linked only by the
# editor; gated on VKM_WITH_EDITOR so a runtime build never compiles Assimp.
if(VKM_WITH_EDITOR)
    add_library(EngineCooker STATIC ${ENGINECOOKER_SOURCES})

    target_include_directories(EngineCooker PRIVATE
        ${PROJECT_SOURCE_DIR}/modules/vkmGL/modules/stb
    )

    target_link_libraries(EngineCooker PUBLIC
        EngineCore
        EngineTools        # generators + shared headers on the include path
    )
    target_link_libraries(EngineCooker PRIVATE
        assimp             # model import, used by loader/model_loader.cpp
        vkm_warnings
        BuildInfo
    )
endif()
