# EngineTools - asset loaders and procedural geometry/material generators.
file(GLOB_RECURSE ENGINETOOLS_SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(EngineTools STATIC ${ENGINETOOLS_SOURCES})

target_include_directories(EngineTools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# stb_image (header-only) for texture decoding.
target_include_directories(EngineTools PRIVATE
    ${PROJECT_SOURCE_DIR}/modules/vkmGL/modules/stb
)

target_link_libraries(EngineTools PUBLIC
    EngineCore
)
target_link_libraries(EngineTools PRIVATE
    assimp             # model import, used privately by loader/model_loader.cpp
    vkm_warnings
    BuildInfo          # resolves relative asset paths against APP_ROOT_DIR
)
