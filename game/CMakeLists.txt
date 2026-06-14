# game - concrete gameplay behaviors. Built two ways:
#  - `game` (STATIC, here): linked into the runtime exe (shipped-game style; no
#    hot-reload). Registration is called directly from the runtime's main.
#  - `game_module` (SHARED, in app/engine_editor): the same sources compiled as a
#    library the editor dlopen's for hot-reload, resolving engine symbols from the
#    editor exe. game_module.cpp (the extern "C" entry) is module-only, so it is
#    excluded from the static flavor.
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS *.cpp)
list(REMOVE_ITEM GAME_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/game_module.cpp)

add_library(game STATIC ${GAME_SOURCES})

target_include_directories(game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(game
    PUBLIC  EngineCore
    PRIVATE vkm_warnings
)
