// What the UI draws: where each element lands, what each quad carries, and the order they draw in.

#include <algorithm>
#include <vector>

#include "core/frame/loading_frame.h"
#include "ecs/component/ui/ui_button.h"
#include "ecs/component/ui/ui_text.h"
#include "resource/asset/texture_asset.h"
#include "system/ui/text_layout.h"

#include "harness/check.h"
#include "ui/screen.h"

using namespace Vkm::Test;

namespace {

/// Whether @p draw holds a quad filled with @p color.
bool holdsColor(const UIDrawData& draw, const glm::vec4& color) {
    return std::any_of(draw.quads.begin(), draw.quads.end(), [&](const UIQuad& q) { return q.top == color; });
}

/// How many of @p draw's quads are glyphs.
size_t glyphsIn(const UIDrawData& draw) {
    return static_cast<size_t>(std::count_if(draw.quads.begin(), draw.quads.end(), [](const UIQuad& q) {
        return q.style.x == UI_TEXT_MARK;
    }));
}

UIText words(const std::string& said, UIText::Align align = UIText::Align::Left) {
    UIText text;
    text.text      = said;
    text.font      = FONT;
    text.pixelSize = 20.0f;  // twice the font's baked height
    text.align     = align;
    return text;
}

} // namespace

TEST("anchor, pivot, position, size and a share of the parent place an element, scaled with its canvas") {
    // Half the reference height: a scaled canvas draws a reference pixel as half a screen pixel.
    Screen screen(960, 540);
    const EntityId canvas = UI::canvas(screen.scene(), "HUD");
    const EntityId panel  = screen.image(canvas, topLeft({100.0f, 50.0f}, {800.0f, 400.0f}));

    UIElement corner;
    corner.anchor   = {1.0f, 1.0f};
    corner.pivot    = {1.0f, 1.0f};
    corner.position = {-20.0f, -10.0f};
    corner.size     = {100.0f, 60.0f};
    const EntityId inCorner = screen.image(panel, corner);

    UIElement half;
    half.relativeSize = {0.5f, 0.0f};
    half.size         = {0.0f, 40.0f};
    const EntityId band = screen.image(panel, half);

    const EntityId fixedCanvas = UI::canvas(screen.scene(), "Fixed");
    screen.scene().get<UICanvas>(fixedCanvas).scaleMode = UICanvas::ScaleMode::Fixed;
    const EntityId fixed = screen.image(fixedCanvas, topLeft({100.0f, 50.0f}, {800.0f, 400.0f}));

    const UIDrawData& draw = screen.show();

    CHECK(screen.rectOf(panel).pos == glm::vec2(50.0f, 25.0f));
    CHECK(screen.rectOf(panel).size == glm::vec2(400.0f, 200.0f));
    // Its bottom right pinned to the panel's, 10 by 5 screen pixels in.
    CHECK(screen.rectOf(inCorner).pos == glm::vec2(390.0f, 190.0f));
    CHECK(screen.rectOf(inCorner).size == glm::vec2(50.0f, 30.0f));
    // Half the panel's width, centered on it.
    CHECK(screen.rectOf(band).pos == glm::vec2(150.0f, 115.0f));
    CHECK(screen.rectOf(band).size == glm::vec2(200.0f, 20.0f));
    CHECK(screen.rectOf(fixed).pos == glm::vec2(100.0f, 50.0f));
    CHECK(screen.rectOf(fixed).size == glm::vec2(800.0f, 400.0f));
    // The quad is drawn over the rect laid out.
    REQUIRE(draw.quads.size() == 4);
    CHECK(draw.quads[1].rect == glm::vec4(390.0f, 190.0f, 440.0f, 220.0f));
}

TEST("canvases draw in sort order, and a later one of the same order draws on top") {
    Screen screen;
    Scene& scene = screen.scene();
    const glm::vec4 green(0.0f, 1.0f, 0.0f, 1.0f), red(1.0f, 0.0f, 0.0f, 1.0f), blue(0.0f, 0.0f, 1.0f, 1.0f);
    const EntityId top   = UI::canvas(scene, "Top", 5);
    const EntityId under = UI::canvas(scene, "Under", -1);
    const EntityId later = UI::canvas(scene, "Later", 5);
    screen.image(top, topLeft({0.0f, 0.0f}, {100.0f, 100.0f}), green);
    screen.image(under, topLeft({0.0f, 0.0f}, {100.0f, 100.0f}), red);
    screen.image(later, topLeft({0.0f, 0.0f}, {100.0f, 100.0f}), blue);

    const UIDrawData& draw = screen.show();

    REQUIRE(draw.quads.size() == 3);
    CHECK(draw.quads[0].top == red);
    CHECK(draw.quads[1].top == green);
    CHECK(draw.quads[2].top == blue);

    scene.get<UICanvas>(under).sortOrder = 9;
    screen.show();
    CHECK(draw.quads[2].top == red);
}

TEST("an image's tint, corners, border, gradient and picture reach the draw, scaled with the canvas") {
    Screen screen(960, 540);
    Scene& scene = screen.scene();
    const EntityId canvas = UI::canvas(scene, "HUD");
    const TextureHandle wood  = screen.frame.resources.add(TextureAsset{}, "test:wood");
    const TextureHandle stone = screen.frame.resources.add(TextureAsset{}, "test:stone");

    const EntityId shaped = screen.image(
        canvas,
        topLeft({0.0f, 0.0f}, {200.0f, 100.0f}),
        {1.0f, 0.0f, 0.0f, 0.5f}
    );
    UIShape& shape = scene.get<UIImage>(shaped).shape;
    shape.cornerRadius = 8.0f;
    shape.borderWidth  = 2.0f;
    shape.borderColor  = {0.0f, 0.0f, 1.0f, 1.0f};
    shape.gradient     = true;
    shape.bottomColor  = {0.0f, 1.0f, 0.0f, 1.0f};
    screen.image(canvas, topLeft({0.0f, 200.0f}, {200.0f, 100.0f}), {0.2f, 0.4f, 0.6f, 1.0f});
    const EntityId planks = screen.image(canvas, topLeft({0.0f, 400.0f}, {200.0f, 100.0f}));
    const EntityId wall   = screen.image(canvas, topLeft({0.0f, 600.0f}, {200.0f, 100.0f}));
    scene.get<UIImage>(planks).texture = wood;
    scene.get<UIImage>(wall).texture   = stone;

    const UIDrawData& draw = screen.show();

    REQUIRE(draw.quads.size() == 4);
    const UIQuad& s = draw.quads[0];
    CHECK(s.top == glm::vec4(1.0f, 0.0f, 0.0f, 0.5f));
    CHECK(s.bottom == glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
    CHECK(s.border == glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
    // Corners and border in screen pixels: half the reference pixels.
    CHECK(s.style == glm::vec3(4.0f, 1.0f, 0.0f));
    // No gradient: the fill is flat, top to bottom.
    CHECK(draw.quads[1].bottom == draw.quads[1].top);
    CHECK(draw.quads[1].style.z == 0.0f);
    // A picture is tinted by the run that samples it, and the run samples that picture; a flat quad
    // samples none, whatever run it shares.
    CHECK(draw.quads[2].style.z == 1.0f);
    CHECK(draw.quads[3].style.z == 1.0f);
    for (const UIDrawCmd& cmd : draw.commands) {
        for (uint32_t q = cmd.firstQuad; q < cmd.firstQuad + cmd.quadCount; ++q) {
            if (q == 2) CHECK(cmd.image == wood);
            if (q == 3) CHECK(cmd.image == stone);
        }
    }
}

TEST("text lines up, wraps and kerns as measureText measures it") {
    Screen screen;
    Scene& scene = screen.scene();
    const EntityId canvas = UI::canvas(scene, "HUD");
    const FontAsset& font = screen.frame.resources.get(screen.frame.resources.findByName<FontAsset>(FONT));
    auto label = [&](const glm::vec2& at, const glm::vec2& size, UIText text) {
        const EntityId id = screen.box(canvas, topLeft(at, size));
        scene.add(id, std::move(text));
        return id;
    };
    // "AVA" at twice the baked size: 20 a glyph, the kerned pair 6 together.
    const float width = measureText(font, "AVA", 20.0f);
    CHECK(width == 54.0f);

    const EntityId left    = label({100.0f, 100.0f}, {200.0f, 40.0f}, words("AVA"));
    const UIDrawData& draw = screen.show();
    REQUIRE(draw.quads.size() == 3);
    // A glyph's ink starts 2 in from its pen and its top 16 over the baseline, 16 under the box's top.
    CHECK(draw.quads[0].rect == glm::vec4(102.0f, 100.0f, 118.0f, 120.0f));
    CHECK(draw.quads[1].rect.x == 116.0f);
    CHECK(draw.quads[2].rect.x == 136.0f);
    CHECK(draw.quads[2].rect.x + 18.0f - draw.quads[0].rect.x + 2.0f == width);
    REQUIRE(draw.commands.size() == 1);
    CHECK(draw.commands[0].font == screen.frame.resources.findByName<FontAsset>(FONT));

    // Centered and right-aligned by the measured width.
    scene.get<UIText>(left).align = UIText::Align::Center;
    screen.show();
    CHECK(draw.quads[0].rect.x == 175.0f);
    scene.get<UIText>(left).align = UIText::Align::Right;
    screen.show();
    CHECK(draw.quads[0].rect.x == 248.0f);

    // Middle of a 40-high box: the block is 20 high, from ascent to descent.
    scene.get<UIText>(left).valign = UIText::VAlign::Middle;
    screen.show();
    CHECK(draw.quads[0].rect.y == 110.0f);

    // Wrapped at the last space that fits: a second line, 24 down; the space draws nothing.
    UIText wrapped = words("AAAA AAAA");
    scene.remove<UIText>(left);
    wrapped.wrap = true;
    label({100.0f, 100.0f}, {100.0f, 40.0f}, wrapped);
    screen.show();
    REQUIRE(draw.quads.size() == 8);
    CHECK(draw.quads[3].rect.y == 100.0f);
    CHECK(draw.quads[4].rect == glm::vec4(102.0f, 124.0f, 118.0f, 144.0f));
}

TEST("text's shadow is drawn under it, a sixteenth of its height down and right") {
    Screen screen;
    const EntityId canvas = UI::canvas(screen.scene(), "HUD");
    UIText text = words("AV");
    text.shadow = {0.0f, 0.0f, 0.0f, 0.5f};
    const EntityId id = screen.box(canvas, topLeft({100.0f, 100.0f}, {200.0f, 40.0f}));
    screen.scene().add(id, text);

    const UIDrawData& draw = screen.show();

    REQUIRE(draw.quads.size() == 4);
    CHECK(draw.quads[0].top == text.shadow);
    CHECK(draw.quads[0].rect.x == draw.quads[2].rect.x + 1.25f);
    CHECK(draw.quads[0].rect.y == draw.quads[2].rect.y + 1.25f);
    CHECK(draw.quads[2].top == text.color);
}

TEST("a stack lays its children out in a column or a row, a hidden child closing up") {
    Screen screen;
    Scene& scene = screen.scene();
    const EntityId canvas = UI::canvas(scene, "HUD");
    const EntityId column = UI::column(scene, canvas, topLeft({100.0f, 100.0f}, {300.0f, 500.0f}), 10.0f);
    std::vector<EntityId> rows;
    for (const float high : {40.0f, 60.0f, 30.0f}) {
        rows.push_back(screen.image(column, topLeft({0.0f, 0.0f}, {200.0f, high})));
    }
    const EntityId row = UI::row(scene, canvas, topLeft({100.0f, 700.0f}, {300.0f, 50.0f}), 5.0f);
    std::vector<EntityId> icons;
    for (const float wide : {50.0f, 60.0f, 30.0f}) {
        icons.push_back(screen.image(row, topLeft({0.0f, 0.0f}, {wide, 50.0f})));
    }

    screen.show();
    CHECK(screen.rectOf(rows[0]).pos == glm::vec2(100.0f, 100.0f));
    CHECK(screen.rectOf(rows[1]).pos == glm::vec2(100.0f, 150.0f));
    CHECK(screen.rectOf(rows[2]).pos == glm::vec2(100.0f, 220.0f));
    CHECK(screen.rectOf(icons[1]).pos == glm::vec2(155.0f, 700.0f));
    CHECK(screen.rectOf(icons[2]).pos == glm::vec2(220.0f, 700.0f));

    scene.get<UIElement>(rows[1]).visible = false;
    scene.get<UIElement>(icons[0]).opacity = 0.0f;
    screen.show();
    CHECK(screen.rectOf(rows[2]).pos == glm::vec2(100.0f, 150.0f));
    CHECK(screen.rectOf(icons[2]).pos == glm::vec2(165.0f, 700.0f));
}

TEST("opacity multiplies down a subtree, and a hidden or faded-out subtree draws nothing") {
    Screen screen;
    Scene& scene = screen.scene();
    const EntityId canvas = UI::canvas(scene, "HUD");
    const EntityId panel  = screen.image(canvas, topLeft({0.0f, 0.0f}, {400.0f, 400.0f}));
    const EntityId inner  = screen.image(
        panel,
        topLeft({10.0f, 10.0f}, {300.0f, 300.0f}),
        {1.0f, 1.0f, 1.0f, 0.8f}
    );
    const EntityId label = screen.box(inner, topLeft({0.0f, 0.0f}, {300.0f, 40.0f}));
    scene.add(label, words("A"));
    scene.get<UIImage>(inner).shape.borderWidth = 1.0f;
    scene.get<UIElement>(panel).opacity = 0.5f;
    scene.get<UIElement>(inner).opacity = 0.5f;

    const UIDrawData& draw = screen.show();

    REQUIRE(draw.quads.size() == 3);
    CHECK(draw.quads[0].top.a == 0.5f);
    CHECK(draw.quads[1].top.a == 0.2f);
    CHECK(draw.quads[1].border.a == 0.25f);
    CHECK(draw.quads[2].top.a == 0.25f);

    scene.get<UIElement>(inner).visible = false;
    screen.show();
    CHECK(draw.quads.size() == 1);
    scene.get<UIElement>(inner).visible = true;
    scene.get<UIElement>(panel).opacity = 0.0f;
    screen.show();
    CHECK(draw.quads.empty());
}

TEST("the loading screen's own UI is laid out from its own scene, over the world's, and takes the pointer") {
    Screen screen;
    Scene& world = screen.scene();
    const glm::vec4 red(1.0f, 0.0f, 0.0f, 1.0f), green(0.0f, 1.0f, 0.0f, 1.0f), blue(0.0f, 0.0f, 1.0f, 1.0f);
    const EntityId hud    = UI::canvas(world, "HUD");
    const EntityId button = UI::button(world, hud, topLeft({0.0f, 0.0f}, {200.0f, 100.0f}), "");
    world.get<UIButton>(button).normalColor = red;

    // The project's screen, a scene of its own, as LoadingSystem hands it over.
    Scene own;
    const EntityId cover = UI::canvas(own, "Cover");
    UI::panel(own, cover, topLeft({0.0f, 0.0f}, {1920.0f, 1080.0f}), green);
    UI::Style style;
    style.font   = FONT;
    style.button = blue;
    UI::button(own, cover, topLeft({500.0f, 500.0f}, {300.0f, 100.0f}), "AV", style);

    LoadingFrame loading;
    loading.opacity          = 1.0f;
    loading.screen           = &own;
    loading.screenAssets     = screen.frame.resources.epoch();
    screen.frame.ctx.loading = &loading;

    screen.click({100.0f, 50.0f});

    REQUIRE(screen.frame.ctx.cover != nullptr);
    const UIDrawData& covered = *screen.frame.ctx.cover;
    CHECK(holdsColor(covered, green));
    CHECK(holdsColor(covered, blue));
    CHECK(glyphsIn(covered) == 2);
    CHECK(!holdsColor(covered, red));
    CHECK(holdsColor(*screen.frame.ctx.ui, red));
    CHECK(!holdsColor(*screen.frame.ctx.ui, green));
    // Nothing under the cover is pressed.
    CHECK(screen.clicks.empty());
    CHECK(screen.frame.input.pointerOverUI());
}
