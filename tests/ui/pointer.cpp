// The UI under a mouse: what a press, a release, a drag and the wheel do, and what they reach.

#include <vector>

#include "core/input_map.h"
#include "ecs/component/ui/ui_button.h"
#include "ecs/component/ui/ui_choice.h"
#include "ecs/component/ui/ui_scroll.h"
#include "ecs/component/ui/ui_slider.h"
#include "ecs/component/ui/ui_text.h"
#include "ecs/component/ui/ui_toggle.h"

#include "harness/check.h"
#include "ui/screen.h"

using namespace Vkm::Test;
using namespace Vkm::Platform;

namespace {

/// The values @p entity changed to, in order.
std::vector<float> valuesOf(const Screen& screen, EntityId entity) {
    std::vector<float> out;
    for (const UIValueEvent& e : screen.values) {
        if (e.entity == entity) out.push_back(e.value);
    }
    return out;
}

} // namespace

TEST("a press let go over the same button is one click, and while held the button follows a drag") {
    Screen screen;
    Scene& scene = screen.scene();
    const EntityId canvas = UI::canvas(scene, "HUD");
    const EntityId button = UI::button(scene, canvas, topLeft({100.0f, 100.0f}, {200.0f, 100.0f}), "");
    const UIButton& state = scene.get<UIButton>(button);
    const glm::vec2 on  = {200.0f, 150.0f};
    const glm::vec2 off = {800.0f, 800.0f};

    screen.pointAt(on);
    screen.show();
    CHECK(state.state == UIButton::State::Hover);
    screen.press();
    screen.show();
    CHECK(screen.clicks.empty());
    CHECK(state.held);
    CHECK(state.state == UIButton::State::Pressed);

    // Dragged off, still held, the button reads where the pointer went in its own pixels.
    screen.pointAt({400.0f, 150.0f});
    screen.show();
    CHECK(state.held);
    CHECK(state.pointer == glm::vec2(300.0f, 50.0f));
    CHECK(screen.clicks.empty());
    screen.pointAt(on);
    screen.show();
    screen.release();
    screen.show();
    REQUIRE(screen.clicks.size() == 1);
    CHECK(screen.clicks[0].entity == button);
    CHECK(!state.held);

    // Pressed on it and let go off it, or pressed off and let go on: no click.
    screen.press();
    screen.show();
    screen.pointAt(off);
    screen.show();
    screen.release();
    screen.show();
    screen.press();
    screen.show();
    screen.pointAt(on);
    screen.show();
    screen.release();
    screen.show();
    CHECK(screen.clicks.size() == 1);

    // A tap between two frames is a click all the same.
    screen.press();
    screen.release();
    screen.show();
    screen.show();
    CHECK(screen.clicks.size() == 2);

    scene.get<UIButton>(button).interactable = false;
    screen.click(on);
    CHECK(screen.clicks.size() == 2);
    CHECK(state.state == UIButton::State::Disabled);
}

TEST("the topmost blocking element takes the pointer, so a click on a menu fires no weapon") {
    Screen screen;
    screen.frame.input.setActions({{"Fire", {InputBinding::mouse(MouseButton::Left)}}});
    Scene& scene = screen.scene();
    const EntityId canvas = UI::canvas(scene, "HUD");
    const EntityId button = UI::button(scene, canvas, topLeft({100.0f, 100.0f}, {200.0f, 100.0f}), "");
    // Drawn after the button, over its right half.
    const EntityId panel   = screen.image(canvas, topLeft({200.0f, 100.0f}, {200.0f, 100.0f}));
    const UIDrawData& draw = screen.show();

    screen.pointAt({250.0f, 150.0f});
    screen.press();
    screen.show();
    CHECK(draw.pointerTarget == panel);
    CHECK(screen.frame.input.pointerOverUI());
    CHECK(screen.frame.input.held(InputActions::UI_CLICK));
    CHECK(!screen.frame.input.held("Fire"));
    screen.release();
    screen.show();
    CHECK(screen.clicks.empty());

    screen.click({150.0f, 150.0f});
    CHECK(draw.pointerTarget == button);
    CHECK(screen.clicks.size() == 1);

    // Over nothing of the UI, the press is the game's.
    screen.pointAt({800.0f, 800.0f});
    screen.press();
    screen.show();
    CHECK(!draw.pointerTarget);
    CHECK(screen.frame.input.held("Fire"));
    screen.release();
    screen.show();

    // An overlay that lets the pointer through: the button under it takes the click.
    scene.get<UIElement>(panel).blocksPointer = false;
    screen.show();
    screen.click({250.0f, 150.0f});
    CHECK(screen.clicks.size() == 2);
}

TEST("an element that clips its children bounds what they draw and what they take") {
    Screen screen;
    Scene& scene = screen.scene();
    const EntityId canvas = UI::canvas(scene, "HUD");
    const EntityId window = screen.box(canvas, topLeft({100.0f, 100.0f}, {200.0f, 100.0f}));
    scene.get<UIElement>(window).clipChildren = true;
    // Half inside the window, half past its right edge.
    const EntityId button = UI::button(scene, window, topLeft({100.0f, 0.0f}, {200.0f, 100.0f}), "");

    const UIDrawData& draw = screen.show();
    REQUIRE(draw.commands.size() == 1);
    CHECK(draw.commands[0].clip.pos == glm::vec2(100.0f, 100.0f));
    CHECK(draw.commands[0].clip.size == glm::vec2(200.0f, 100.0f));

    screen.click({350.0f, 150.0f});
    CHECK(screen.clicks.empty());
    CHECK(!draw.pointerTarget);
    screen.click({250.0f, 150.0f});
    REQUIRE(screen.clicks.size() == 1);
    CHECK(screen.clicks[0].entity == button);

    scene.get<UIElement>(window).clipChildren = false;
    screen.show();
    screen.click({350.0f, 150.0f});
    CHECK(screen.clicks.size() == 2);
    CHECK(draw.commands[0].clip.size == glm::vec2(1920.0f, 1080.0f));
}

TEST("a scroll view measures its content, and the wheel moves the axis that overflows, vertical first") {
    Screen screen;
    Scene& scene = screen.scene();
    const EntityId canvas = UI::canvas(scene, "HUD");
    // A list 100 high holding five rows of 50; a strip 200 wide holding five tiles of 100; and a
    // sheet holding both.
    auto view = [&](const glm::vec2& at, const glm::vec2& tile, const glm::vec2& step) {
        const EntityId id = screen.box(canvas, topLeft(at, {200.0f, 100.0f}));
        scene.add(id, UIScroll{});
        for (int i = 0; i < 5; ++i) screen.image(id, topLeft(step * static_cast<float>(i), tile));
        return id;
    };
    const EntityId list  = view({100.0f, 100.0f}, {200.0f, 50.0f}, {0.0f, 50.0f});
    const EntityId strip = view({400.0f, 100.0f}, {100.0f, 100.0f}, {100.0f, 0.0f});
    const EntityId sheet = view({700.0f, 100.0f}, {100.0f, 50.0f}, {100.0f, 50.0f});
    std::vector<EntityId> rows;
    HierarchyOperations::forEachChild(scene, list, [&](EntityId row) { rows.push_back(row); });
    const UIScroll& listScroll = scene.get<UIScroll>(list);

    screen.show();
    CHECK(listScroll.contentSize == glm::vec2(200.0f, 250.0f));
    CHECK(listScroll.viewSize == glm::vec2(200.0f, 100.0f));
    CHECK(scene.get<UIScroll>(strip).contentSize == glm::vec2(500.0f, 100.0f));

    // A notch toward the viewer moves the content one wheel step on; the rows follow next frame.
    screen.pointAt(screen.middleOf(list));
    screen.turnWheel(-1.0f);
    screen.show();
    CHECK(listScroll.offset == glm::vec2(0.0f, 60.0f));
    screen.show();
    CHECK(screen.rectOf(rows[0]).pos.y == 40.0f);
    screen.turnWheel(-10.0f);
    screen.show();
    CHECK(listScroll.offset.y == 150.0f);

    screen.pointAt(screen.middleOf(strip));
    screen.turnWheel(-1.0f);
    screen.show();
    CHECK(scene.get<UIScroll>(strip).offset == glm::vec2(60.0f, 0.0f));

    screen.pointAt(screen.middleOf(sheet));
    screen.turnWheel(-1.0f);
    screen.show();
    CHECK(scene.get<UIScroll>(sheet).offset == glm::vec2(0.0f, 60.0f));

    // The wheel over nothing scrolls nothing.
    screen.pointAt({100.0f, 900.0f});
    screen.turnWheel(1.0f);
    screen.show();
    CHECK(listScroll.offset.y == 150.0f);
}

TEST("sliders, toggles and choices change under the pointer and say so, and a value the game writes says nothing") {
    Screen screen;
    Scene& scene = screen.scene();
    const EntityId canvas = UI::canvas(scene, "HUD");
    UI::Style style;
    style.font = FONT;
    const EntityId slider = UI::slider(
        scene,
        canvas,
        topLeft({100.0f, 100.0f}, {400.0f, 40.0f}),
        0.0f,
        10.0f,
        2.0f,
        style
    );
    const EntityId toggle = UI::toggle(
        scene,
        canvas,
        topLeft({100.0f, 200.0f}, {80.0f, 40.0f}),
        false,
        style
    );
    const EntityId choice = UI::choice(
        scene,
        canvas,
        topLeft({100.0f, 300.0f}, {300.0f, 40.0f}),
        {"Low", "Medium", "High"},
        0,
        style
    );
    const UIChoice& steps = scene.get<UIChoice>(choice);
    screen.show();

    // Pressed a quarter along the track, then dragged past its end.
    screen.pointAt({200.0f, 120.0f});
    screen.press();
    screen.show();
    CHECK(scene.get<UISlider>(slider).value == 2.5f);
    // Its fill grows to the value the frame it changes.
    CHECK(screen.rectOf(scene.get<UISlider>(slider).fill).size.x == 100.0f);
    screen.pointAt({900.0f, 120.0f});
    screen.show();
    screen.release();
    screen.show();
    CHECK(valuesOf(screen, slider) == std::vector<float>({2.5f, 10.0f}));
    scene.get<UISlider>(slider).step = 3.0f;
    screen.click({260.0f, 120.0f});
    CHECK(scene.get<UISlider>(slider).value == 3.0f);

    screen.click(screen.middleOf(toggle));
    CHECK(scene.get<UIToggle>(toggle).on);
    screen.click(screen.middleOf(toggle));
    CHECK(valuesOf(screen, toggle) == std::vector<float>({1.0f, 0.0f}));

    screen.click(screen.middleOf(steps.next));
    CHECK(steps.index == 1);
    CHECK(scene.get<UIText>(choice).text == "Medium");
    // Past either end a step stays, and says nothing: Low does not wrap to High.
    screen.click(screen.middleOf(steps.back));
    screen.click(screen.middleOf(steps.back));
    CHECK(steps.index == 0);
    for (int i = 0; i < 3; ++i) screen.click(screen.middleOf(steps.next));
    CHECK(steps.index == 2);
    CHECK(valuesOf(screen, choice) == std::vector<float>({1.0f, 0.0f, 1.0f, 2.0f}));

    screen.values.clear();
    scene.get<UISlider>(slider).value = 5.0f;
    scene.get<UIToggle>(toggle).on    = true;
    scene.get<UIChoice>(choice).index = 1;
    screen.show();
    CHECK(screen.values.empty());
    CHECK(scene.get<UIText>(choice).text == "Medium");
    CHECK(screen.rectOf(scene.get<UISlider>(slider).fill).size.x == 200.0f);
}
