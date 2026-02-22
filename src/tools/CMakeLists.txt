# ============================================================================
# Engine Tools Library (Optional utilities)
# ============================================================================
file(GLOB_RECURSE ENGINETOOLS_SOURCES *.cpp)

add_library(EngineTools STATIC ${ENGINETOOLS_SOURCES})

target_include_directories(EngineTools PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/vkmEngine/tools>
)

target_include_directories(EngineTools PRIVATE
    ${PROJECT_SOURCE_DIR}/modules/vkmGL/modules/stb
)

target_link_libraries(EngineTools PUBLIC
    EngineCore
)
