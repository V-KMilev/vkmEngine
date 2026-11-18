# vkm_loader - what the runtime decodes or bakes itself: plain images, HDR environments and the
# SDF font. Every host that builds an Engine links it; the heavy importers live in vkm_pipeline, so
# a runtime build pulls no model importer.
file(GLOB VKM_LOADER_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_library(vkm_loader STATIC ${VKM_LOADER_SOURCES})

target_include_directories(vkm_loader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vkm_loader PUBLIC
    vkm_core
)
target_link_libraries(vkm_loader PRIVATE
    stb                # the HDR and plain image decodes
    stb_truetype       # the font bake; a static link, so a host that bakes no font takes none of it
    vkm_warnings
)
