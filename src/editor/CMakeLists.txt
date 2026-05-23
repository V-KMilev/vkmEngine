# ============================================================================
# Engine Editor Library
# ============================================================================
file(GLOB_RECURSE ENGINEEDITOR_SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(EngineEditor STATIC ${ENGINEEDITOR_SOURCES})

target_include_directories(EngineEditor PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/vkmEngine/editor>
)

# stb_image_write for viewport screenshot capture. PRIVATE - only the
# screenshot.cpp translation unit defines STB_IMAGE_WRITE_IMPLEMENTATION.
target_include_directories(EngineEditor PRIVATE
    ${PROJECT_SOURCE_DIR}/modules/vkmGL/modules/stb
)

target_link_libraries(EngineEditor PUBLIC
    EngineCore
    EngineTools
    imgui
    vkmGL
)
