# EngineApp - the shared bootstrap: wires the engine-tier systems, render
# pipeline, and default scene into an Engine instance. Linked by both the
# editor and runtime binaries, so the only difference between them is the editor.
file(GLOB_RECURSE ENGINEAPP_SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(EngineApp STATIC ${ENGINEAPP_SOURCES})

target_include_directories(EngineApp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(EngineApp PUBLIC
    EngineCore
    EngineRendering
    EngineTools
    BackendOpenGL
)
target_link_libraries(EngineApp PRIVATE
    vkm_warnings
    BuildInfo
)
