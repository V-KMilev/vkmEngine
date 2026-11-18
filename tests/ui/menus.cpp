// A menu that pauses the game, in a running game.

#include <vector>

#include "core/input_map.h"
#include "ecs/component/ui/ui_build.h"
#include "ecs/component/ui/ui_canvas.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;
using namespace Vkm::Platform;

TEST("a pausing menu holds the world and frees the cursor while it shows, its action showing and hiding it") {
    Game game;
    game.engine.getInput().setActions({{"Menu", {InputBinding::key(Key::Escape)}}});
    Scene& scene = game.scene();
    const EntityId menu = UI::canvas(scene, "Pause");
    UICanvas& canvas = scene.get<UICanvas>(menu);
    canvas.pausesGame   = true;
    canvas.visible      = false;
    canvas.toggleAction = "Menu";
    InputHandle& keys = game.engine.getWindow().getInputHandle();

    // The world runs on its own time from the second frame; Escape is struck after frames 2 and 5.
    std::vector<float> passed;
    std::vector<bool>  shown, freed;
    game.run(8, [&](int frame) {
        if (frame == 1) game.clock().setPaused(false);
        passed.push_back(game.clock().getSimDelta());
        shown.push_back(scene.get<UICanvas>(menu).visible);
        freed.push_back(game.engine.getWindow().cursorFree());
        keys.onKeyEvent(Key::Escape, frame == 2 || frame == 5);
    }, 0);

    // Shown in frame 3 and hidden in frame 6; the clock is held from the frame after each shows.
    CHECK(shown == std::vector<bool>({false, false, true, true, true, false, false, false}));
    CHECK(freed == shown);
    CHECK(passed[1] > 0.0f);
    CHECK(passed[3] == 0.0f);
    CHECK(passed[4] == 0.0f);
    CHECK(passed[5] == 0.0f);
    CHECK(passed[6] > 0.0f);
    CHECK(!game.clock().isMenuShown());
}

TEST("a pausing menu leaves a world the host paused to the host: its steps run, its cursor stays") {
    Game game;
    const EntityId menu = UI::canvas(game.scene(), "Pause");
    game.scene().get<UICanvas>(menu).pausesGame = true;

    // Game::run's world is paused and stepped, as an editor steps one.
    int ticks = 0;
    game.run(5, [&](int) { ticks = static_cast<int>(game.clock().getTick()); });

    CHECK(ticks == 5);
    CHECK(!game.clock().isMenuShown());
    CHECK(!game.engine.getWindow().cursorFree());
}
