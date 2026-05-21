# ============================================================================
# Engine Editor Library
# ============================================================================
file(GLOB_RECURSE ENGINEEDITOR_SOURCES CONFIGURE_DEPENDS *.cpp)
# SystemMetrics uses GL for VRAM queries - compiled here, not in EngineCore
list(APPEND ENGINEEDITOR_SOURCES ${PROJECT_SOURCE_DIR}/src/engine/platform/system_metrics.cpp)

add_library(EngineEditor STATIC ${ENGINEEDITOR_SOURCES})

target_include_directories(EngineEditor PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/vkmEngine/editor>
)

target_link_libraries(EngineEditor PUBLIC
    EngineCore
    EngineTools
    imgui
    vkmGL
)
