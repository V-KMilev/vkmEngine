#include "support.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_image.h"
#include "ecs/component/ui/ui_scroll.h"
#include "ecs/component/ui/ui_text.h"
#include "system/ui/ui_system.h"
#include "system/ui/ui_events.h"
#include "ecs/component/ui/ui_button.h"
#include "ecs/hierarchy_operations.h"
#include "platform/input/input_handle.h"
#include "platform/input/input_map.h"
#include "loader/font_baker.h"
#include "core/utf8.h"
#include "resource/asset/font_asset.h"
#include "system/ui/text_layout.h"
#include "io/scene/component_serializer.h"
#include "io/scene/scene_serializer.h"

namespace {

// Round numbers, so an expected rect can be worked out from the anchors by hand.
constexpr uint32_t VIEW_W = 800;
constexpr uint32_t VIEW_H = 600;

// Runs one UISystem frame over a scene. The chrome states the viewport because no
// window does; with none, the layout pass would see a zero-size window and bail.
struct UIFrame {
    TestFrame frame;
    UISystem  system;

    explicit UIFrame(Scene& scene) : frame(scene) {
        frame.chrome.setViewport(0, 0, VIEW_W, VIEW_H);
    }

    const UIDrawData& run() {
        system.update(frame.ctx);
        return *frame.ctx.ui;
    }
};

constexpr bool PRESSED  = true;
constexpr bool RELEASED = false;

// Point and turn the wheel as a device does. The viewport is unscaled at the origin
// here, so a window pixel is the viewport pixel UISystem resolves against.
void pointAt(UIFrame& ui, glm::vec2 at, float wheel = 0.0f, bool down = false) {
    InputHandle device;
    device.moveTo(at.x, at.y);
    device.setButton(0, down);
    if (wheel != 0.0f) device.addScroll(wheel);
    ui.frame.input.update(device, ui.frame.chrome);
}

// One authored pixel per screen pixel, so a test asserts layout, not scale.
EntityId addCanvas(Scene& scene, int32_t sortOrder = 0) {
    const EntityId id = scene.createEntity();
    UICanvas canvas;
    canvas.scaleMode = UICanvas::ScaleMode::Fixed;
    canvas.sortOrder = sortOrder;
    scene.add(id, std::move(canvas));
    return id;
}

EntityId addElement(
    Scene& scene,
    EntityId parent,
    glm::vec2 anchor,
    glm::vec2 pivot,
    glm::vec2 position,
    glm::vec2 size
) {
    const EntityId id = scene.createEntity();
    UIElement element = UIElement::at(anchor, position, size);
    // Pivot set separately on purpose: like-to-like pinning cannot tell anchor from pivot.
    element.pivot = pivot;
    scene.add(id, std::move(element));
    HierarchyOperations::setParent(scene, id, parent);
    return id;
}

// Top-left anchored, so the rect is exactly position and size.
EntityId addBox(Scene& scene, EntityId parent, glm::vec2 position, glm::vec2 size) {
    return addElement(scene, parent, {0.0f, 0.0f}, {0.0f, 0.0f}, position, size);
}

bool sameRect(const UIRect& rect, glm::vec2 pos, glm::vec2 size) {
    return nearly(rect.pos.x, pos.x)   && nearly(rect.pos.y, pos.y)
        && nearly(rect.size.x, size.x) && nearly(rect.size.y, size.y);
}

void testLayoutResolvesRects() {
    std::printf("Where an element lands:\n");

    Scene scene;
    const EntityId canvas = addCanvas(scene);
    const EntityId panel  = addBox(scene, canvas, {20.0f, 30.0f}, {200.0f, 100.0f});
    // Anchor picks the parent's middle, pivot this one's, so it centres whatever its size.
    const EntityId label  = addElement(
        scene,
        panel,
        {0.5f, 0.5f},
        {0.5f, 0.5f},
        {0.0f, 0.0f},
        {40.0f, 20.0f}
    );
    // Pinned to the parent's bottom-right corner by its own bottom-right corner.
    const EntityId corner = addElement(
        scene,
        panel,
        {1.0f, 1.0f},
        {1.0f, 1.0f},
        {0.0f, 0.0f},
        {10.0f, 10.0f}
    );

    UIFrame ui(scene);
    ui.run();

    check(
        "a top-left element sits at its position",
        sameRect(scene.get<UIElement>(panel).screenRect, {20.0f, 30.0f}, {200.0f, 100.0f})
    );
    check(
        "a centred child centres in the parent, not the viewport",
        sameRect(scene.get<UIElement>(label).screenRect, {100.0f, 70.0f}, {40.0f, 20.0f})
    );
    check(
        "a bottom-right pin ends flush with the parent's corner",
        sameRect(scene.get<UIElement>(corner).screenRect, {210.0f, 120.0f}, {10.0f, 10.0f})
    );
}

void testTheCommonCaseHasAName() {
    std::printf("An element pinned like-to-like:\n");

    Scene scene;
    const EntityId canvas = addCanvas(scene);
    const EntityId id     = scene.createEntity();
    scene.add(id, UIElement::at({1.0f, 0.5f}, {-10.0f, 4.0f}, {80.0f, 20.0f}));
    HierarchyOperations::setParent(scene, id, canvas);

    UIFrame ui(scene);
    ui.run();

    const UIElement& element = scene.get<UIElement>(id);
    check(
        "at() sets the anchor and the pivot to one value",
        element.anchor == element.pivot && nearly(element.anchor.x, 1.0f) && nearly(element.anchor.y, 0.5f)
    );
    // The viewport's right edge, its own right edge there, nudged in ten and down four
    // from the vertical middle.
    check(
        "  and the rect resolves where that pins it",
        sameRect(
            element.screenRect,
            {static_cast<float>(VIEW_W) - 10.0f - 80.0f, static_cast<float>(VIEW_H) * 0.5f + 4.0f - 10.0f},
            {80.0f, 20.0f}
        )
    );
    check(
        "  leaving the other fields at their defaults",
        element.visible && element.blocksPointer && !element.clipChildren
    );
}

void testAnElementCanFillItsParent() {
    std::printf("An element sized by its parent:\n");

    Scene scene;
    const EntityId canvas = addCanvas(scene);
    // Two screen pixels per authored one, so a share of the parent is seen not to be
    // scaled twice.
    UICanvas& settings = scene.get<UICanvas>(canvas);
    settings.scaleMode       = UICanvas::ScaleMode::ScaleWithHeight;
    settings.referenceHeight = static_cast<float>(VIEW_H) * 0.5f;

    const EntityId scrim = addBox(scene, canvas, {0.0f, 0.0f}, {0.0f, 0.0f});
    scene.get<UIElement>(scrim).relativeSize = {1.0f, 1.0f};

    const EntityId panel = addBox(scene, canvas, {10.0f, 10.0f}, {100.0f, 50.0f});
    const EntityId inset = addElement(
        scene,
        panel,
        {0.5f, 0.5f},
        {0.5f, 0.5f},
        {0.0f, 0.0f},
        {-20.0f, -10.0f}
    );
    scene.get<UIElement>(inset).relativeSize = {1.0f, 1.0f};
    const EntityId bar = addBox(scene, panel, {0.0f, 0.0f}, {0.0f, 4.0f});
    scene.get<UIElement>(bar).relativeSize = {0.5f, 0.0f};
    const EntityId gone = addBox(scene, panel, {0.0f, 0.0f}, {-500.0f, 4.0f});
    scene.get<UIElement>(gone).relativeSize = {1.0f, 0.0f};

    const EntityId view = addBox(scene, panel, {0.0f, 0.0f}, {30.0f, 0.0f});
    scene.get<UIElement>(view).relativeSize = {0.0f, 1.0f};
    scene.add(view, UIScroll{});

    UIFrame ui(scene);
    ui.run();
    check(
        "a full share fills the viewport, whatever the canvas scale",
        sameRect(scene.get<UIElement>(scrim).screenRect, {0.0f, 0.0f}, {800.0f, 600.0f})
    );
    check(
        "a full share less a margin is an inset on every side",
        sameRect(scene.get<UIElement>(inset).screenRect, {40.0f, 30.0f}, {160.0f, 80.0f})
    );
    check(
        "a half share on one axis spans half the parent there",
        sameRect(scene.get<UIElement>(bar).screenRect, {20.0f, 20.0f}, {100.0f, 8.0f})
    );
    check(
        "a margin wider than the share leaves nothing rather than less than nothing",
        nearly(scene.get<UIElement>(gone).screenRect.size.x, 0.0f)
    );
    check(
        "a scroll view sized by its parent sees the window it was given",
        nearly(scene.get<UIScroll>(view).viewSize.y, 50.0f)
    );
}

void testCanvasScaleAndVisibility() {
    std::printf("What the canvas decides:\n");

    Scene scene;
    const EntityId canvas = addCanvas(scene);
    // Half the viewport height, so every authored pixel is worth two.
    UICanvas& settings = scene.get<UICanvas>(canvas);
    settings.scaleMode       = UICanvas::ScaleMode::ScaleWithHeight;
    settings.referenceHeight = static_cast<float>(VIEW_H) * 0.5f;

    const EntityId panel = addBox(scene, canvas, {10.0f, 10.0f}, {100.0f, 50.0f});
    const EntityId child = addBox(scene, panel,  {0.0f, 0.0f},   {10.0f, 10.0f});
    scene.add(panel, UIImage{});

    UIFrame ui(scene);
    check("a visible canvas draws what is on it", !ui.run().commands.empty());
    check(
        "ScaleWithHeight scales position and size together",
        sameRect(scene.get<UIElement>(panel).screenRect, {20.0f, 20.0f}, {200.0f, 100.0f})
    );

    scene.get<UIElement>(panel).visible = false;
    scene.get<UIElement>(child).screenRect = UIRect{};
    ui.run();
    check(
        "an invisible element takes its whole subtree with it",
        sameRect(scene.get<UIElement>(child).screenRect, {0.0f, 0.0f}, {0.0f, 0.0f})
    );

    scene.get<UIElement>(panel).visible = true;
    scene.get<UICanvas>(canvas).visible = false;
    const UIDrawData& hidden = ui.run();
    check("a hidden canvas draws nothing at all", hidden.commands.empty());

    // An element outside every canvas is never seeded (see hasCanvasAncestor). Measured
    // against a drawing canvas, so "nothing added" differs from "nothing draws".
    scene.get<UICanvas>(canvas).visible = true;
    const size_t drawnUnderCanvas = ui.run().commands.size();

    const EntityId orphan = scene.createEntity();
    scene.add(orphan, UIElement{});
    scene.add(orphan, UIImage{});
    check(
        "an element under no canvas is not laid out",
        !hasCanvasAncestor(scene, orphan) && ui.run().commands.size() == drawnUnderCanvas
    );
}

void testCommandsMergeUntilTheStateChanges() {
    std::printf("How many draws a screen of panels costs:\n");

    Scene scene;
    const EntityId canvas = addCanvas(scene);
    for (int i = 0; i < 8; ++i) {
        const EntityId panel = addBox(scene, canvas, {static_cast<float>(i) * 20.0f, 0.0f}, {10.0f, 10.0f});
        scene.add(panel, UIImage{});
    }

    UIFrame ui(scene);
    const UIDrawData& drawn = ui.run();
    check("eight solid panels are one draw call", drawn.commands.size() == 1);
    check("  covering all eight quads", drawn.vertices.size() == 8 * 6);

    // A clip is draw state, so a run under another clip never merges with the one
    // before, however adjacent.
    Scene clipped;
    const EntityId root = addCanvas(clipped);
    const EntityId a    = addBox(clipped, root, {0.0f, 0.0f}, {50.0f, 50.0f});
    clipped.add(a, UIImage{});
    const EntityId box  = addBox(clipped, root, {0.0f, 60.0f}, {50.0f, 50.0f});
    clipped.get<UIElement>(box).clipChildren = true;
    const EntityId inner = addBox(clipped, box, {0.0f, 0.0f}, {10.0f, 10.0f});
    clipped.add(inner, UIImage{});

    UIFrame clippedUI(clipped);
    const UIDrawData& two = clippedUI.run();
    check("a clipped run does not merge into an unclipped one", two.commands.size() == 2);
}

void testClippingBoundsPixelsAndPointer() {
    std::printf("What a clip contains:\n");

    Scene scene;
    const EntityId canvas = addCanvas(scene);
    const EntityId panel  = addBox(scene, canvas, {0.0f, 0.0f}, {100.0f, 100.0f});
    scene.get<UIElement>(panel).clipChildren = true;
    // Half in, half out of the panel; and one entirely outside it.
    const EntityId inside  = addBox(scene, panel, {50.0f, 50.0f}, {100.0f, 100.0f});
    scene.add(inside, UIImage{});
    const EntityId outside = addBox(scene, panel, {200.0f, 200.0f}, {50.0f, 50.0f});
    scene.add(outside, UIImage{});

    UIFrame ui(scene);
    const UIDrawData& drawn = ui.run();

    check(
        "a clipped child draws under the panel's rect, not the viewport's",
        !drawn.commands.empty() && sameRect(drawn.commands.front().clip, {0.0f, 0.0f}, {100.0f, 100.0f})
    );
    // The rect stays whole: clipping bounds what is shown, not where layout put it,
    // which lets a scroll view move it back in.
    check(
        "  while the element's own rect is unchanged",
        sameRect(scene.get<UIElement>(inside).screenRect, {50.0f, 50.0f}, {100.0f, 100.0f})
    );

    // The pointer is at the viewport origin: a blocker over it takes it, one clipped
    // away from it does not.
    Scene hit;
    const EntityId hitCanvas = addCanvas(hit);
    const EntityId over      = addBox(hit, hitCanvas, {0.0f, 0.0f}, {40.0f, 40.0f});
    hit.add(over, UIImage{});
    UIFrame hitUI(hit);
    check("a blocker under the pointer is reported", hitUI.run().pointerTarget == over);
    check("  and the input map is told", hitUI.frame.input.pointerOverUI());

    // The same blocker, inside a panel that does not reach the origin.
    const EntityId away = addBox(hit, hitCanvas, {100.0f, 100.0f}, {40.0f, 40.0f});
    hit.get<UIElement>(away).clipChildren = true;
    HierarchyOperations::setParent(hit, over, away);
    hit.get<UIElement>(over).position = {-100.0f, -100.0f};
    check("a blocker clipped away from the pointer is not", !hitUI.run().pointerTarget);
    check("  and the input map is told that too", !hitUI.frame.input.pointerOverUI());
}

void testScrollMeasuresClampsAndShifts() {
    std::printf("What a scroll view does:\n");

    Scene scene;
    const EntityId canvas = addCanvas(scene);
    const EntityId view   = addBox(scene, canvas, {0.0f, 0.0f}, {100.0f, 100.0f});
    scene.add(view, UIScroll{});
    // Three rows of 60: 180 of content in a 100-tall window.
    EntityId rows[3];
    for (int i = 0; i < 3; ++i) {
        rows[i] = addBox(scene, view, {0.0f, static_cast<float>(i) * 60.0f}, {80.0f, 60.0f});
        scene.add(rows[i], UIImage{});
    }

    UIFrame ui(scene);
    const UIDrawData& drawn = ui.run();

    const UIScroll& scroll = scene.get<UIScroll>(view);
    check(
        "the content measures itself from what is in it",
        nearly(scroll.contentSize.y, 180.0f) && nearly(scroll.contentSize.x, 80.0f)
    );
    check(
        "the view is the element's own size",
        nearly(scroll.viewSize.y, 100.0f) && nearly(scroll.viewSize.x, 100.0f)
    );
    check("what is left to scroll is the difference", nearly(scroll.range().y, 80.0f));
    check("  and nothing on the axis that fits", nearly(scroll.range().x, 0.0f));
    check(
        "a scroll view clips whether or not the element asked to",
        !drawn.commands.empty() && sameRect(drawn.commands.front().clip, {0.0f, 0.0f}, {100.0f, 100.0f})
    );

    // An offset moves the content, not the window.
    scene.get<UIScroll>(view).offset = {0.0f, 50.0f};
    ui.run();
    check(
        "scrolling moves the content up by the offset",
        sameRect(scene.get<UIElement>(rows[0]).screenRect, {0.0f, -50.0f}, {80.0f, 60.0f})
    );
    check(
        "  and leaves the window where the layout put it",
        sameRect(scene.get<UIElement>(view).screenRect, {0.0f, 0.0f}, {100.0f, 100.0f})
    );

    // Clamped, so a caller can write a big number to mean "the bottom".
    scene.get<UIScroll>(view).offset = {0.0f, 10000.0f};
    ui.run();
    check(
        "an offset past the content is clamped to the end",
        nearly(scene.get<UIScroll>(view).offset.y, 80.0f)
    );

    scene.get<UIScroll>(view).offset = {0.0f, -50.0f};
    ui.run();
    check("and a negative one back to the start", nearly(scene.get<UIScroll>(view).offset.y, 0.0f));

    // A row appended and "the bottom" asked in the same frame: clamped against the
    // content as it now measures, so the next frame shows the new last row.
    const EntityId added = addBox(scene, view, {0.0f, 180.0f}, {80.0f, 60.0f});
    scene.add(added, UIImage{});
    scene.get<UIScroll>(view).offset = {0.0f, 1.0e9f};
    ui.run();
    ui.run();
    check(
        "the bottom asked for as a row is added reaches that row",
        nearly(scene.get<UIScroll>(view).offset.y, 140.0f)
            && nearly(scene.get<UIElement>(added).screenRect.max().y, 100.0f)
    );
    scene.get<UIElement>(added).visible = false;

    // Content shrinking below the window resets the offset to zero rather than
    // stranding the view.
    for (EntityId row : rows) scene.get<UIElement>(row).visible = false;
    ui.run();
    ui.run();
    check(
        "content that no longer overflows scrolls back to the top",
        nearly(scene.get<UIScroll>(view).offset.y, 0.0f)
            && nearly(scene.get<UIScroll>(view).contentSize.y, 0.0f)
    );
}

// Baking a real font is the only way to get glyph metrics without GL. False where the font
// file is not beside the tests, as in a packaged SDK.
bool addTestFont(ResourceManager& resources) {
    const std::filesystem::path ttf =
        std::filesystem::path(VKM_ENGINE_DIR) / "assets" / "fonts" / "Roboto-Medium.ttf";
    if (!std::filesystem::exists(ttf)) return false;
    return static_cast<bool>(bakeFontSDF(resources, ttf.string(), "ui:test", 32.0f));
}

void testContentIsWhatWasDrawn() {
    std::printf("What counts as content:\n");

    Scene scene;
    const EntityId canvas = addCanvas(scene);
    const EntityId view   = addBox(scene, canvas, {0.0f, 0.0f}, {100.0f, 100.0f});
    scene.add(view, UIScroll{});

    // An empty layout box, far taller than the window and below its top. An empty place
    // is nothing to scroll to; a list counting it would run past its last row.
    addBox(scene, view, {0.0f, 300.0f}, {80.0f, 400.0f});

    UIFrame ui(scene);
    ui.run();
    check(
        "an empty layout box is not content, nor is where it sits",
        nearly(scene.get<UIScroll>(view).contentSize.y, 0.0f)
    );

    // The same box, now drawing something.
    EntityId filled{};
    scene.forEach<UIElement>([&](EntityId id, UIElement& e) {
        if (id != view && nearly(e.size.y, 400.0f)) filled = id;
    });
    scene.add(filled, UIImage{});
    ui.run();
    check("  and is, the moment it draws", nearly(scene.get<UIScroll>(view).contentSize.y, 700.0f));

    // Nor a text box with nothing to draw. Off the screen's left, the origin is to its
    // right, so a zero corner for "nothing" would invent a horizontal range.
    Scene offscreen;
    const EntityId offCanvas = addCanvas(offscreen);
    const EntityId offView   = addBox(offscreen, offCanvas, {-200.0f, 0.0f}, {100.0f, 100.0f});
    offscreen.add(offView, UIScroll{});
    const EntityId blank = addBox(offscreen, offView, {30.0f, 0.0f}, {50.0f, 50.0f});
    offscreen.add(blank, UIText{});

    UIFrame offUI(offscreen);
    offUI.run();
    check(
        "  and an empty text box is not content, even off the edge of the screen",
        nearly(offscreen.get<UIScroll>(offView).contentSize.x, 0.0f)
    );

    // A text box measures its text, not its rect - wrong, scrolling to the bottom finds
    // nothing.
    Scene worded;
    const EntityId wordCanvas = addCanvas(worded);
    const EntityId wordView   = addBox(worded, wordCanvas, {0.0f, 0.0f}, {200.0f, 60.0f});
    worded.add(wordView, UIScroll{});
    const EntityId label = addBox(worded, wordView, {0.0f, 0.0f}, {180.0f, 600.0f});

    UIFrame wordedUI(worded);
    if (!addTestFont(wordedUI.frame.resources)) {
        std::printf("  (no font beside the tests - text measurement skipped)\n");
        return;
    }
    UIText text;
    text.font      = "ui:test";
    text.pixelSize = 12.0f;
    text.text      = "one\ntwo";
    worded.add(label, std::move(text));

    wordedUI.run();
    const float measured = worded.get<UIScroll>(wordView).contentSize.y;
    check("a paragraph measures its lines, not its box", measured > 0.0f && measured < 100.0f);
}

// A press and release over a button is one click, on the release frame. The button's
// state must follow, since that recolours it.
void testAPressAndAReleaseIsOneClick() {
    std::printf("What a press and a release over a button do:\n");

    Scene scene;
    const EntityId canvas = addCanvas(scene);
    const EntityId button = addBox(scene, canvas, {0.0f, 0.0f}, {80.0f, 40.0f});
    UIButton play;
    play.eventId = "play";
    scene.add(button, std::move(play));

    UIFrame ui(scene);
    // A reset map defines the engine's click again, as a new one does.
    ui.frame.input.reset();

    std::vector<std::string> clicks;
    ui.frame.events.subscribe<UIClickEvent>([&](const UIClickEvent& e) { clicks.push_back(e.eventId); });

    pointAt(ui, {20.0f, 20.0f});
    ui.run();
    check("hovering it is not a click", clicks.empty());
    check("  and it says it is hovered", scene.get<UIButton>(button).state == UIButton::State::Hover);

    pointAt(ui, {20.0f, 20.0f}, 0.0f, PRESSED);
    ui.run();
    ui.frame.events.flush();
    check("pressing it is not a click either", clicks.empty());
    check("  and it says it is pressed", scene.get<UIButton>(button).state == UIButton::State::Pressed);

    pointAt(ui, {20.0f, 20.0f}, 0.0f, RELEASED);
    ui.run();
    ui.frame.events.flush();
    check("letting go over it fires one click", clicks.size() == 1);
    check("  carrying the button's own event id", !clicks.empty() && clicks[0] == "play");

    ui.run();
    ui.frame.events.flush();
    check("and a frame with nothing happening fires none", clicks.size() == 1);

    // A press leaving the button before release is the gesture every pointer UI lets
    // you take back.
    pointAt(ui, {20.0f, 20.0f}, 0.0f, PRESSED);
    ui.run();
    pointAt(ui, {400.0f, 400.0f}, 0.0f, RELEASED);
    ui.run();
    ui.frame.events.flush();
    check("a press dragged off the button fires nothing", clicks.size() == 1);
}

// The editor's chrome shares the pointer with the game's UI. A press let go over a
// panel was ended by the chrome; left armed, the next release over that button - a
// press begun on the chrome - would fire a click nobody made.
void testAReleaseTheChromeTakesDisarmsThePress() {
    std::printf("A press let go over the host's chrome:\n");

    Scene scene;
    const EntityId canvas = addCanvas(scene);
    const EntityId button = addBox(scene, canvas, {0.0f, 0.0f}, {80.0f, 40.0f});
    UIButton play;
    play.eventId = "play";
    scene.add(button, std::move(play));

    UIFrame ui(scene);

    std::vector<std::string> clicks;
    ui.frame.events.subscribe<UIClickEvent>([&](const UIClickEvent& e) { clicks.push_back(e.eventId); });

    // Pressed on the button, dragged onto a panel and let go there.
    pointAt(ui, {20.0f, 20.0f}, 0.0f, PRESSED);
    ui.run();
    ui.frame.chrome.setCapture(true, false);
    pointAt(ui, {400.0f, 400.0f}, 0.0f, RELEASED);
    ui.run();
    ui.frame.events.flush();
    check("a release the chrome takes is not a click", clicks.empty());

    // Pressed on the chrome, carried back over the button and let go.
    pointAt(ui, {400.0f, 400.0f}, 0.0f, PRESSED);
    ui.run();
    ui.frame.chrome.setCapture(false, false);
    pointAt(ui, {20.0f, 20.0f}, 0.0f, PRESSED);
    ui.run();
    pointAt(ui, {20.0f, 20.0f}, 0.0f, RELEASED);
    ui.run();
    ui.frame.events.flush();
    check("  nor is a press the chrome began, let go over the button", clicks.empty());
}

// A menu over the world shares the mouse with the gun. A press begun on the menu is
// the menu's - gameplay never hears it - and one begun in the world stays gameplay's
// dragged over a panel. Gameplay cannot ask the UI (ctx.ui is no behavior's to reach),
// so the map decides.
void testAPressOnTheUIIsTheUIs() {
    std::printf("A press that begins over the UI:\n");

    Scene scene;
    const EntityId canvas = addCanvas(scene);
    const EntityId button = addBox(scene, canvas, {0.0f, 0.0f}, {80.0f, 40.0f});
    UIButton resume;
    resume.eventId = "resume";
    scene.add(button, std::move(resume));

    UIFrame ui(scene);
    ui.frame.input.define("Fire", { InputBinding{InputSource::MouseButton, 0, 1.0f} });

    std::vector<std::string> clicks;
    ui.frame.events.subscribe<UIClickEvent>([&](const UIClickEvent& e) { clicks.push_back(e.eventId); });

    pointAt(ui, {20.0f, 20.0f});
    ui.run();
    pointAt(ui, {20.0f, 20.0f}, 0.0f, PRESSED);
    check("a press over a button reaches the UI's click", ui.frame.input.pressed(InputActions::UI_CLICK));
    check("  and not the gameplay action on the same button", !ui.frame.input.held("Fire"));
    ui.run();
    ui.frame.input.beginTick(1);
    check("  nor the tick's command", !ui.frame.input.pressed(ui.frame.input.command(), "Fire"));

    pointAt(ui, {400.0f, 400.0f}, 0.0f, PRESSED);
    ui.run();
    check("dragged off the UI, it is still not gameplay's", !ui.frame.input.held("Fire"));
    pointAt(ui, {20.0f, 20.0f}, 0.0f, PRESSED);
    ui.run();
    pointAt(ui, {20.0f, 20.0f}, 0.0f, RELEASED);
    ui.run();
    ui.frame.events.flush();
    check("  and letting go over the button is still a click", clicks.size() == 1);

    pointAt(ui, {400.0f, 400.0f});
    ui.run();
    pointAt(ui, {400.0f, 400.0f}, 0.0f, PRESSED);
    ui.run();
    check("a press in the world is gameplay's", ui.frame.input.pressed("Fire"));
    pointAt(ui, {20.0f, 20.0f}, 0.0f, PRESSED);
    ui.run();
    check("  and stays gameplay's carried over the UI", ui.frame.input.held("Fire"));
    // The previous frame's layout found the button under the pointer, so a press
    // beginning now is the UI's.
    pointAt(ui, {20.0f, 20.0f}, 0.0f, PRESSED);
    ui.run();
    check("  on every frame it is held there", ui.frame.input.held("Fire"));
    check("  without letting go on the way", !ui.frame.input.released("Fire"));
    pointAt(ui, {20.0f, 20.0f}, 0.0f, RELEASED);
    ui.run();
    ui.frame.events.flush();
    check("  where letting go is no click", clicks.size() == 1);
}

// A slider is a button over its track, read while held. A press begun on it must stay
// its own dragged past the end, and the pointer must arrive in authored space - a
// project converting spaces itself would miss by the editor viewport's offset.
void testAHeldButtonFollowsTheDrag() {
    std::printf("A press dragged from a button:\n");

    Scene scene;
    const EntityId canvas = scene.createEntity();
    UICanvas scaled;
    scaled.scaleMode       = UICanvas::ScaleMode::ScaleWithHeight;
    scaled.referenceHeight = static_cast<float>(VIEW_H) * 0.5f;
    scene.add(canvas, std::move(scaled));
    const EntityId track = addBox(scene, canvas, {10.0f, 10.0f}, {40.0f, 20.0f});
    scene.add(track, UIButton{});

    UIFrame ui(scene);

    pointAt(ui, {90.0f, 30.0f});
    ui.run();
    const UIButton& button = scene.get<UIButton>(track);
    check(
        "the pointer reads in the element's own pixels",
        nearly(button.pointer.x, 35.0f) && nearly(button.pointer.y, 5.0f)
    );
    check("  and hovering is not holding", !button.held);

    pointAt(ui, {90.0f, 30.0f}, 0.0f, PRESSED);
    ui.run();
    check("a press on it holds it", button.held);

    pointAt(ui, {300.0f, 30.0f}, 0.0f, PRESSED);
    ui.run();
    check("  and still does dragged past its end", button.held);
    check("  where the pointer reads past its width", nearly(button.pointer.x, 140.0f));

    pointAt(ui, {300.0f, 30.0f}, 0.0f, RELEASED);
    ui.run();
    check("letting go lets go", !button.held);

    pointAt(ui, {300.0f, 30.0f}, 0.0f, PRESSED);
    ui.run();
    pointAt(ui, {90.0f, 30.0f}, 0.0f, PRESSED);
    ui.run();
    check("a press that began elsewhere never holds it", !button.held);

    // A track at half its parent's width is not its authored size, and a slider divides
    // the pointer by the size it has.
    pointAt(ui, {90.0f, 30.0f});
    ui.run();
    scene.get<UIElement>(track).relativeSize = {0.5f, 0.0f};
    ui.run();
    const float laidOut = scene.get<UIElement>(track).screenRect.size.x / 2.0f;
    check(
        "a track sized by its parent says the size it was laid out at",
        nearly(button.resolvedSize.x, laidOut)
            && button.resolvedSize.x > 40.0f
            && nearly(button.resolvedSize.y, 20.0f)
    );
}

// With the cursor disabled for mouse-look, GLFW reports a virtual position that drifts
// as the view turns; a HUD panel it crossed would take the gun's click.
void testALockedCursorPointsAtNothing() {
    std::printf("A locked cursor over the UI:\n");

    Scene scene;
    const EntityId canvas = addCanvas(scene);
    const EntityId panel  = addBox(scene, canvas, {0.0f, 0.0f}, {80.0f, 40.0f});
    scene.add(panel, UIButton{});

    UIFrame ui(scene);
    pointAt(ui, {20.0f, 20.0f});
    ui.run();
    check("a free cursor over a panel is over the UI", ui.frame.input.pointerOverUI());

    ui.frame.window.setCursorMode(CursorMode::Disabled);
    ui.run();
    check("a locked one is not", !ui.frame.input.pointerOverUI());
    check("  and hovers nothing", scene.get<UIButton>(panel).state == UIButton::State::Normal);
}

// A button's quad is recoloured after the walk, once its state is known; a gradient is
// its two edges in two colours. Tinting all six vertices would flatten every gradient
// button on its first frame.
void testAButtonKeepsItsFadeInEveryState() {
    std::printf("A button with a gradient:\n");

    Scene scene;
    const EntityId canvas = addCanvas(scene);
    const EntityId button = addBox(scene, canvas, {0.0f, 0.0f}, {80.0f, 40.0f});
    UIButton faded;
    faded.shape.gradient    = true;
    faded.shape.bottomColor = {1.0f, 0.0f, 0.0f, 1.0f};
    scene.add(button, std::move(faded));

    UIFrame ui(scene);
    const UIButton& drawn = scene.get<UIButton>(button);

    // By vertex position, not index: what is on screen, not how the quad is wound.
    const auto fadesFrom = [&](const UIDrawData& data, const glm::vec4& top) {
        if (data.vertices.size() != 6) return false;
        for (const UIVertex& vertex : data.vertices) {
            const bool onBottom = nearly(vertex.pos.y, 40.0f);
            if (vertex.color != (onBottom ? drawn.shape.bottomColor : top)) return false;
        }
        return true;
    };

    pointAt(ui, {400.0f, 400.0f});
    const UIDrawData& resting = ui.run();
    check(
        "a resting button fades from its normal tint to its bottom colour",
        drawn.state == UIButton::State::Normal && fadesFrom(resting, drawn.normalColor)
    );

    pointAt(ui, {20.0f, 20.0f});
    const UIDrawData& hovered = ui.run();
    check(
        "  and a hovered one from its hover tint, to the same bottom colour",
        drawn.state == UIButton::State::Hover && fadesFrom(hovered, drawn.hoverColor)
    );
}

// The wheel follows the click: whatever is topmost under the pointer decides, and the
// wheel travels up to the nearest scroll view holding it. Otherwise a pause menu over
// an inventory takes the click while the inventory takes the wheel.
void testTheWheelGoesWhereTheClickGoes() {
    std::printf("Which scroll view the wheel reaches:\n");

    Scene scene;
    // The overlay's canvas is made FIRST: storage order is [over, canvas], sorted order
    // [canvas, over], so only UISystem's sort puts the overlay on top.
    const EntityId over   = addCanvas(scene, 5);
    const EntityId canvas = addCanvas(scene);
    const EntityId view   = addBox(scene, canvas, {0.0f, 0.0f}, {100.0f, 100.0f});
    scene.add(view, UIImage{});
    scene.add(view, UIScroll{});
    // Twice the window's height, so there is somewhere to scroll.
    const EntityId content = addBox(scene, view, {0.0f, 0.0f}, {100.0f, 200.0f});
    scene.add(content, UIImage{});

    UIFrame ui(scene);

    // One frame to measure: a view learns its content by walking it, so the first wheel
    // it can honour is the second frame's.
    pointAt(ui, {50.0f, 50.0f});
    ui.run();
    check("the view has content to scroll", nearly(scene.get<UIScroll>(view).range().y, 100.0f));

    // Toward the viewer, down a list: wheel() is positive away, and the view subtracts.
    pointAt(ui, {50.0f, 50.0f}, -1.0f);
    ui.run();
    const float scrolled = scene.get<UIScroll>(view).offset.y;
    check("the wheel over the view scrolls it", scrolled > 0.0f);

    // The higher canvas draws over it, with a blocker outside the view: same pointer,
    // same wheel. Its canvas existed all along - an empty one blocks nothing.
    const EntityId panel = addBox(scene, over, {0.0f, 0.0f}, {200.0f, 200.0f});
    scene.add(panel, UIImage{});

    pointAt(ui, {50.0f, 50.0f}, -1.0f);
    ui.run();
    check("a panel drawn over it takes the wheel away", nearly(scene.get<UIScroll>(view).offset.y, scrolled));

    // With the pointer off both, nothing moves either.
    scene.get<UIElement>(panel).visible = false;
    pointAt(ui, {400.0f, 400.0f}, -1.0f);
    ui.run();
    check("and the wheel elsewhere is nobody's", nearly(scene.get<UIScroll>(view).offset.y, scrolled));

    // A list inside an opaque panel that draws nothing, the pointer over the list beside
    // its rows: the panel is the topmost blocker, the list inside it, so the list scrolls.
    Scene boxed;
    const EntityId boxCanvas = addCanvas(boxed);
    const EntityId window    = addBox(boxed, boxCanvas, {0.0f, 0.0f}, {200.0f, 200.0f});
    boxed.add(window, UIImage{});
    const EntityId list = addBox(boxed, window, {0.0f, 0.0f}, {100.0f, 100.0f});
    boxed.add(list, UIScroll{});
    const EntityId column = addBox(boxed, list, {0.0f, 0.0f}, {40.0f, 300.0f});
    boxed.add(column, UIImage{});

    UIFrame boxUI(boxed);
    pointAt(boxUI, {80.0f, 50.0f});
    boxUI.run();
    pointAt(boxUI, {80.0f, 50.0f}, -1.0f);
    boxUI.run();
    check(
        "a list with no picture of its own, inside a panel, takes the wheel",
        boxed.get<UIScroll>(list).offset.y > 0.0f
    );
}

void testScrollDoesNotGrowAnOuterScroll() {
    std::printf("A list inside a list:\n");

    Scene scene;
    const EntityId canvas = addCanvas(scene);
    const EntityId outer  = addBox(scene, canvas, {0.0f, 0.0f}, {200.0f, 200.0f});
    scene.add(outer, UIScroll{});
    const EntityId inner  = addBox(scene, outer, {0.0f, 0.0f}, {100.0f, 100.0f});
    scene.add(inner, UIScroll{});
    // Far taller than either window.
    const EntityId tall = addBox(scene, inner, {0.0f, 0.0f}, {50.0f, 5000.0f});
    scene.add(tall, UIImage{});

    UIFrame ui(scene);
    ui.run();

    check(
        "the inner list measures its own content",
        nearly(scene.get<UIScroll>(inner).contentSize.y, 5000.0f)
    );
    // The outer content is the inner view's rect, which fits; what the inner view
    // hides cannot make the outer scrollable.
    check(
        "the outer list measures the inner view, not what it hides",
        nearly(scene.get<UIScroll>(outer).contentSize.y, 100.0f)
    );
    check("  so the outer list has nothing to scroll", nearly(scene.get<UIScroll>(outer).range().y, 0.0f));
}

// A glyph marks itself in its own vertex, so a button and its label share draw state:
// a column of labelled buttons under one font is one draw call.
void testLabelledButtonsAreOneDraw() {
    std::printf("What a column of labelled buttons costs:\n");

    Scene scene;
    const EntityId canvas = addCanvas(scene);

    UIFrame ui(scene);
    if (!addTestFont(ui.frame.resources)) {
        std::printf("  (no font beside the tests - skipped)\n");
        return;
    }

    for (int i = 0; i < 4; ++i) {
        const EntityId button = addBox(scene, canvas, {0.0f, static_cast<float>(i) * 50.0f}, {120.0f, 40.0f});
        scene.add(button, UIButton{});
        UIText label;
        label.font      = "ui:test";
        label.pixelSize = 12.0f;
        label.text      = "go";
        scene.add(button, std::move(label));
    }

    const UIDrawData& drawn = ui.run();
    check("four labelled buttons are one draw call", drawn.commands.size() == 1);
    check("  which samples the labels' atlas", !drawn.commands.empty() && drawn.commands[0].font);

    bool marked = drawn.vertices.size() > 6;
    for (size_t v = 0; v < drawn.vertices.size(); ++v) {
        const bool glyph = (v % (6 + 2 * 6)) >= 6;  // each button: its quad, then two glyphs
        marked &= (drawn.vertices[v].shape.z < 0.0f) == glyph;
    }
    check("  and only the glyphs carry the text mark", marked);
}

void testTextBreaksIntoLines() {
    std::printf("How many lines a string draws as:\n");

    Scene scene;
    const EntityId canvas = addCanvas(scene);
    const EntityId label  = addBox(scene, canvas, {0.0f, 0.0f}, {60.0f, 200.0f});

    UIFrame ui(scene);
    if (!addTestFont(ui.frame.resources)) {
        std::printf("  (no font beside the tests - skipped)\n");
        return;
    }

    UIText text;
    text.font      = "ui:test";
    text.pixelSize = 10.0f;
    text.text      = "ab";
    scene.add(label, std::move(text));

    ui.run();
    const size_t oneLine = ui.frame.ctx.ui->vertices.size();
    check("a two-glyph line is two quads", oneLine == 2 * 6);

    // The glyph count is the same; the second's position is what shows the newline.
    const float firstTop = ui.frame.ctx.ui->vertices[0].pos.y;
    scene.get<UIText>(label).text = "a\nb";
    ui.run();
    check("a newline still draws both glyphs", ui.frame.ctx.ui->vertices.size() == 2 * 6);
    check(
        "  with the second one on a line below the first",
        ui.frame.ctx.ui->vertices[6].pos.y > firstTop + 1.0f
    );

    // Wrapping is off by default, so a long line runs past the rect - what a HUD
    // readout wants.
    scene.get<UIText>(label).text = "aaaa bbbb cccc dddd";
    ui.run();
    const float unwrappedBottom = ui.frame.ctx.ui->vertices.back().pos.y;

    scene.get<UIText>(label).wrap = true;
    ui.run();
    check(
        "wrapping breaks a line too wide for the element",
        ui.frame.ctx.ui->vertices.back().pos.y > unwrappedBottom + 1.0f
    );

    // A word wider than the rect has no space to break at, and must still be bounded.
    scene.get<UIText>(label).text = "aaaaaaaaaaaaaaaaaaaaaaaa";
    ui.run();
    float widest = 0.0f;
    for (const UIVertex& vertex : ui.frame.ctx.ui->vertices) widest = std::max(widest, vertex.pos.x);
    check("an unbreakable word is broken mid-word rather than overflowing", widest < 60.0f + 20.0f);
}

// The top-left corner of each glyph quad one UIText drew, in draw order.
std::vector<glm::vec2> glyphCorners(const UIDrawData& drawn) {
    std::vector<glm::vec2> corners;
    for (size_t v = 0; v + 5 < drawn.vertices.size(); v += 6) {
        if (drawn.vertices[v].shape.z < 0.0f) corners.push_back(drawn.vertices[v].pos);
    }
    return corners;
}

// Glyphs per line, top first, and each line's first glyph start. Lines are told apart
// by where their quads end below, a line step apart.
struct DrawnLines {
    std::vector<int>   glyphs;
    std::vector<float> left;
};

DrawnLines drawnLines(const UIDrawData& drawn) {
    std::vector<std::pair<float, float>> quads;   // (bottom, left)
    for (size_t v = 0; v + 5 < drawn.vertices.size(); v += 6) {
        if (drawn.vertices[v].shape.z < 0.0f) {
            quads.emplace_back(drawn.vertices[v + 2].pos.y, drawn.vertices[v].pos.x);
        }
    }
    std::sort(quads.begin(), quads.end());
    DrawnLines lines;
    float lineBottom = -1.0e9f;
    for (const auto& [bottom, left] : quads) {
        if (bottom > lineBottom + 3.0f) {
            lines.glyphs.push_back(0);
            lines.left.push_back(left);
            lineBottom = bottom;
        }
        ++lines.glyphs.back();
        lines.left.back() = std::min(lines.left.back(), left);
    }
    return lines;
}

// A wrapped line takes every word that fits; the breaking space goes with the break,
// so neither line starts with one.
void testWrapBreaksAfterTheLastWordThatFits() {
    std::printf("Where a wrapped line breaks:\n");

    Scene scene;
    const EntityId canvas = addCanvas(scene);
    const EntityId label  = addBox(scene, canvas, {0.0f, 0.0f}, {1.0f, 200.0f});

    UIFrame ui(scene);
    if (!addTestFont(ui.frame.resources)) {
        std::printf("  (no font beside the tests - skipped)\n");
        return;
    }
    const FontAsset& font = ui.frame.resources.get(ui.frame.resources.findByName<FontAsset>("ui:test"));

    UIText text;
    text.font      = "ui:test";
    text.pixelSize = 10.0f;
    text.wrap      = true;
    text.text      = "aa bb cc";
    scene.add(label, std::move(text));

    scene.get<UIElement>(label).size.x = measureText(font, "aa bb", 10.0f) + 0.25f;
    DrawnLines lines = drawnLines(ui.run());
    check(
        "a line exactly as wide as two words holds both",
        lines.glyphs.size() == 2 && lines.glyphs[0] == 4 && lines.glyphs[1] == 2
    );

    scene.get<UIText>(label).text = "aaa aa";
    scene.get<UIElement>(label).size.x = measureText(font, "aaa", 10.0f) + 0.25f;
    lines = drawnLines(ui.run());
    check(
        "a break where only a space overflows takes the space with it",
        lines.glyphs.size() == 2
            && lines.glyphs[0] == 3
            && lines.glyphs[1] == 2
            && nearly(lines.left[0], lines.left[1])
    );
}

void testKerningIsOneWalkForMeasureAndDraw() {
    std::printf("Where a kerned pair lands:\n");

    Scene scene;
    const EntityId canvas = addCanvas(scene);
    const EntityId label  = addBox(scene, canvas, {0.0f, 0.0f}, {400.0f, 100.0f});

    UIFrame ui(scene);
    if (!addTestFont(ui.frame.resources)) {
        std::printf("  (no font beside the tests - skipped)\n");
        return;
    }
    const FontAsset& font = ui.frame.resources.get(ui.frame.resources.findByName<FontAsset>("ui:test"));

    const int a = FontAsset::glyphIndex(U'A');
    const int v = FontAsset::glyphIndex(U'V');
    check("the face kerns A against V", font.kern(a, v) < 0.0f);
    check("  and not a pair it has no entry for", font.kern(a, FontAsset::glyphIndex(U'|')) == 0.0f);

    constexpr float SIZE  = 64.0f;
    const float     scale = SIZE / font.pixelHeight;
    check(
        "a measured pair is narrower than its two glyphs apart",
        measureText(font, "AV", SIZE) < measureText(font, "A", SIZE) + measureText(font, "V", SIZE) - 0.5f
    );

    UIText text;
    text.font      = "ui:test";
    text.pixelSize = SIZE;
    text.text      = "AV";
    scene.add(label, std::move(text));

    std::vector<glm::vec2> corners = glyphCorners(ui.run());
    const FontGlyph& glyphA = font.glyphs[static_cast<size_t>(a)];
    const FontGlyph& glyphV = font.glyphs[static_cast<size_t>(v)];
    const float drawnStep   = (corners.size() == 2)
        ? (corners[1].x - glyphV.offset.x * scale) - (corners[0].x - glyphA.offset.x * scale)
        : 0.0f;
    check(
        "the drawn V sits one kerned advance after the A",
        corners.size() == 2 && std::abs(drawnStep - (glyphA.advance + font.kern(a, v)) * scale) < 0.01f
    );

    // Right alignment places the line by its width; ignoring kerning would leave the
    // pair short of the edge by the kern.
    scene.get<UIText>(label).align = UIText::Align::Right;
    corners = glyphCorners(ui.run());
    const float penEnd = corners.empty() ? 0.0f
        : corners.back().x - glyphV.offset.x * scale + glyphV.advance * scale;
    check(
        "a right-aligned kerned pair ends on the rect's edge",
        !corners.empty() && std::abs(penEnd - 400.0f) <= 0.5f
    );
}

void testTextPastASCIIDraws() {
    std::printf("Text past ASCII:\n");

    Scene scene;
    const EntityId canvas = addCanvas(scene);
    const EntityId label  = addBox(scene, canvas, {0.0f, 0.0f}, {400.0f, 100.0f});

    UIFrame ui(scene);
    if (!addTestFont(ui.frame.resources)) {
        std::printf("  (no font beside the tests - skipped)\n");
        return;
    }

    UIText text;
    text.font      = "ui:test";
    text.pixelSize = 20.0f;
    text.text      = "\xC3\xA9";   // e acute: two bytes, one character
    scene.add(label, std::move(text));
    check("a two-byte character draws one glyph", glyphCorners(ui.run()).size() == 1);

    scene.get<UIText>(label).text = "20\xC2\xB0" "C \xE2\x80\x94 \xE2\x82\xAC" "5\xE2\x80\xA6";
    check(
        "degrees, a dash, the euro and an ellipsis each draw one glyph",
        glyphCorners(ui.run()).size() == 8
    );

    // A stray continuation byte draws nothing, and reading resumes at the next lead byte.
    scene.get<UIText>(label).text = "a\x80" "b";
    check("a malformed byte draws nothing and costs the rest nothing", glyphCorners(ui.run()).size() == 2);

    std::string typed;
    Utf8::append(typed, U'\u00E9');
    Utf8::append(typed, U'\u20AC');
    Utf8::append(typed, U'x');
    size_t at = 0;
    const char32_t first  = Utf8::next(typed, at);
    const char32_t second = Utf8::next(typed, at);
    check(
        "a character appended reads back as itself",
        first == U'\u00E9' && second == U'\u20AC' && typed.size() == 6
    );
    check(
        "  and a step back from the end lands on the start of the last one",
        Utf8::previous(typed, typed.size()) == 5 && Utf8::previous(typed, 5) == 2
    );

    // A whole character, then a stray continuation byte: next() steps over the stray
    // alone, so stepping back must too.
    const std::string stray = "\xC3\xA9\xA9";
    check(
        "  and a step back over a stray byte steps over it alone, as a walk forward does",
        Utf8::previous(stray, 3) == 2 && Utf8::previous(stray, 2) == 0
    );
}

void testALineStartsOnAWholePixel() {
    std::printf("Where a line of text starts:\n");

    Scene scene;
    const EntityId canvas = addCanvas(scene);
    const EntityId label  = addBox(scene, canvas, {10.3f, 20.6f}, {200.0f, 37.7f});

    UIFrame ui(scene);
    if (!addTestFont(ui.frame.resources)) {
        std::printf("  (no font beside the tests - skipped)\n");
        return;
    }
    const FontAsset& font = ui.frame.resources.get(ui.frame.resources.findByName<FontAsset>("ui:test"));

    UIText text;
    text.font      = "ui:test";
    text.pixelSize = 13.0f;
    text.text      = "Hi\nHi";
    text.valign    = UIText::VAlign::Middle;
    scene.add(label, std::move(text));

    const std::vector<glm::vec2> corners = glyphCorners(ui.run());
    const float     scale = 13.0f / font.pixelHeight;
    const FontGlyph& h    = *font.glyph(U'H');
    bool whole = corners.size() == 4;
    for (size_t line = 0; whole && line < 2; ++line) {
        const glm::vec2 origin = corners[line * 2] - h.offset * scale;
        whole = nearly(origin.x, std::round(origin.x)) && nearly(origin.y, std::round(origin.y));
    }
    check("each line's start and baseline are whole pixels in a fractional rect", whole);
}

// The atlas is sampled at a level whose texel averages a block of full-size ones; a
// glyph nearer its neighbour than that block would bleed into it.
void testGlyphsLeaveRoomForTheMipLevels() {
    std::printf("The room between glyphs in the atlas:\n");

    ResourceManager resources;
    if (!addTestFont(resources)) {
        std::printf("  (no font beside the tests - skipped)\n");
        return;
    }
    const FontAsset& font = resources.get(resources.findByName<FontAsset>("ui:test"));

    const float gutter = static_cast<float>(1u << (FontAsset::MIP_LEVELS - 1));
    const float texels = static_cast<float>(font.atlasSize);
    bool apart = true;
    int  drawn = 0;
    for (size_t i = 0; i < font.glyphs.size(); ++i) {
        const FontGlyph& a = font.glyphs[i];
        if (a.size.x <= 0.0f) continue;
        ++drawn;
        for (size_t j = i + 1; j < font.glyphs.size(); ++j) {
            const FontGlyph& b = font.glyphs[j];
            if (b.size.x <= 0.0f) continue;
            const glm::vec2 gapLo = (b.uvMin - a.uvMax) * texels;
            const glm::vec2 gapHi = (a.uvMin - b.uvMax) * texels;
            const float gap = std::max(std::max(gapLo.x, gapHi.x), std::max(gapLo.y, gapHi.y));
            if (gap < gutter - 0.01f) apart = false;
        }
    }
    check("glyphs past ASCII are in the atlas", drawn > static_cast<int>(FontAsset::ASCII_COUNT));
    check("no two glyphs are closer than the smallest level's texel", apart);
}

void testAPictureBreaksARunOnlyWhereTheImageChanges() {
    std::printf("What pictures cost:\n");

    Scene scene;
    const EntityId canvas = addCanvas(scene);

    UIFrame ui(scene);
    if (!addTestFont(ui.frame.resources)) {
        std::printf("  (no font beside the tests - skipped)\n");
        return;
    }
    const TextureHandle rifle  = ui.frame.resources.add(TextureAsset{}, "ui:rifle");
    const TextureHandle pistol = ui.frame.resources.add(TextureAsset{}, "ui:pistol");

    // A buy-menu row: panel, icon, caption, icon, divider - then another weapon's icon.
    float y = 0.0f;
    const auto add = [&](TextureHandle texture, const char* caption) {
        const EntityId row = addBox(scene, canvas, {0.0f, y}, {120.0f, 20.0f});
        y += 25.0f;
        UIImage image;
        image.texture = texture;
        scene.add(row, std::move(image));
        if (!caption) return;
        UIText label;
        label.font      = "ui:test";
        label.pixelSize = 12.0f;
        label.text      = caption;
        scene.add(row, std::move(label));
    };
    add({}, nullptr);
    add(rifle, nullptr);
    add({}, "AK");
    add(rifle, nullptr);
    add({}, nullptr);
    add(pistol, nullptr);

    const UIDrawData& drawn = ui.run();
    check(
        "pictures, text and flat panels share a draw until the picture changes",
        drawn.commands.size() == 2
    );
    // Siblings walk in hierarchy order, not add order, so run order is not the question.
    const auto runOf = [&](TextureHandle texture) {
        for (const UIDrawCmd& cmd : drawn.commands) if (cmd.image == texture) return &cmd;
        return static_cast<const UIDrawCmd*>(nullptr);
    };
    check(
        "  and each run samples the picture its icons show",
        runOf(rifle) && runOf(pistol) && runOf(rifle)->font
    );

    size_t sampling = 0;
    bool   glyphsPlain = true;
    for (const UIVertex& vertex : drawn.vertices) {
        sampling += vertex.image > 0.5f ? 1 : 0;
        if (vertex.shape.z < 0.0f) glyphsPlain &= vertex.image == 0.0f;
    }
    const bool marked = drawn.vertices.size() == 8 * 6 && sampling == 3 * 6 && glyphsPlain;
    check("  with only the icons marked to sample it", marked);
}

void testAPictureIsSavedByName() {
    std::printf("A picture in a scene file:\n");

    ResourceManager resources;
    const TextureHandle icon = resources.add(TextureAsset{}, "ui:icon");
    UIImage image;
    image.texture = icon;

    const nlohmann::json saved = ComponentSerializer::save(image, resources);
    check("it is written as the texture's name", saved.value("texture", std::string{}) == "ui:icon");

    UIImage loaded;
    ComponentSerializer::load(saved, loaded, resources);
    check("  and read back as the same texture", loaded.texture == icon);

    ComponentSerializer::AssetRefs refs;
    ComponentSerializer::emitAssetRefs(image, refs);
    check("  and named in the scene's assets block", refs.textures.size() == 1 && refs.textures[0] == icon);

    loaded = UIImage{};
    ComponentSerializer::load(nlohmann::json::object(), loaded, resources);
    check("a file naming no picture loads with none", !loaded.texture);
}

void testUIComponentsRoundTrip() {
    std::printf("What a saved UI scene brings back:\n");

    Scene scene;
    ResourceManager resources;

    const EntityId canvas = addCanvas(scene);
    scene.add(canvas, makeName("Canvas"));
    const EntityId view = addBox(scene, canvas, {5.0f, 6.0f}, {70.0f, 80.0f});
    scene.add(view, makeName("List"));
    scene.get<UIElement>(view).clipChildren = true;
    scene.get<UIElement>(view).relativeSize = {0.25f, 1.0f};
    UIScroll scroll;
    scroll.offset    = {3.0f, 12.0f};
    scroll.wheelStep = 41.0f;
    // Resolved fields a save must not carry: the file is what was authored, not some
    // frame's content height.
    scroll.contentSize = {999.0f, 999.0f};
    scene.add(view, std::move(scroll));

    UIText text;
    text.text = "two\nlines";
    text.wrap = true;
    scene.add(view, std::move(text));

    const std::filesystem::path path = runScratch() / "vkm_ui_roundtrip.json";
    check("the scene saves", SceneSerializer::save(scene, resources, path.string()));

    Scene loaded;
    ResourceManager loadedResources;
    check("and loads back", SceneSerializer::load(loaded, loadedResources, path.string()));

    EntityId reloaded{};
    loaded.forEach<UIScroll>([&](EntityId id, UIScroll&) { reloaded = id; });
    check("the scroll view came back", reloaded != EntityId{});
    if (reloaded) {
        const UIScroll& back = loaded.get<UIScroll>(reloaded);
        check("  with its authored offset", nearly(back.offset.y, 12.0f));
        check("  and its wheel step", nearly(back.wheelStep, 41.0f));
        check(
            "  and no measured content, which is this frame's not the file's",
            nearly(back.contentSize.x, 0.0f) && nearly(back.contentSize.y, 0.0f)
        );
        check("the element's clip flag survived", loaded.get<UIElement>(reloaded).clipChildren);
        check(
            "  and its share of the parent",
            nearly(loaded.get<UIElement>(reloaded).relativeSize.x, 0.25f)
                && nearly(loaded.get<UIElement>(reloaded).relativeSize.y, 1.0f)
        );
        check("the text's wrap flag survived", loaded.get<UIText>(reloaded).wrap);
        check("  and its newline", loaded.get<UIText>(reloaded).text == "two\nlines");
    }
    std::filesystem::remove(path);
}

// stb validates neither the length nor the offset it is handed, so bakeFontSDF refuses
// a file too short for the offset table, or whose offset is -1, before stb reads it.
void testAFileThatIsNotAFontIsRefusedRatherThanRead() {
    std::printf("A font file that is not one:\n");

    ResourceManager resources;
    const std::filesystem::path dir = runScratch() / "vkm_font_guard";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    auto write = [&](const char* file, const std::string& bytes) {
        const std::filesystem::path path = dir / file;
        std::ofstream out(path, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.close();
        return path.string();
    };

    // Shorter than the offset table: only the length check prevents an overread.
    check(
        "a one-byte file is refused",
        !bakeFontSDF(resources, write("tiny.ttf", std::string(1, '\0')), "ui:tiny", 32.0f)
    );

    // Long enough to read, not a font: the offset comes back negative.
    check(
        "a file long enough to read but not a font is refused",
        !bakeFontSDF(resources, write("junk.ttf", std::string(4096, 'x')), "ui:junk", 32.0f)
    );

    check(
        "and a file that is not there is refused",
        !bakeFontSDF(resources, (dir / "absent.ttf").string(), "ui:absent", 32.0f)
    );

    std::filesystem::remove_all(dir, ec);
}

} // namespace

void runUITests() {
    testLayoutResolvesRects();
    testTheCommonCaseHasAName();
    testCanvasScaleAndVisibility();
    testAnElementCanFillItsParent();
    testCommandsMergeUntilTheStateChanges();
    testClippingBoundsPixelsAndPointer();
    testScrollMeasuresClampsAndShifts();
    testContentIsWhatWasDrawn();
    testAPressAndAReleaseIsOneClick();
    testAReleaseTheChromeTakesDisarmsThePress();
    testAPressOnTheUIIsTheUIs();
    testAHeldButtonFollowsTheDrag();
    testALockedCursorPointsAtNothing();
    testAButtonKeepsItsFadeInEveryState();
    testTheWheelGoesWhereTheClickGoes();
    testScrollDoesNotGrowAnOuterScroll();
    testTextBreaksIntoLines();
    testWrapBreaksAfterTheLastWordThatFits();
    testLabelledButtonsAreOneDraw();
    testKerningIsOneWalkForMeasureAndDraw();
    testTextPastASCIIDraws();
    testALineStartsOnAWholePixel();
    testGlyphsLeaveRoomForTheMipLevels();
    testAPictureBreaksARunOnlyWhereTheImageChanges();
    testAPictureIsSavedByName();
    testUIComponentsRoundTrip();
    testAFileThatIsNotAFontIsRefusedRatherThanRead();
}
