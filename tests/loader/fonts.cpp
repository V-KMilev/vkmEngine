// The UI's fonts as a host bakes them at startup: at a size the display asks for, every glyph in
// its own place in the atlas, and the face's kerning kept for the text it lays out.

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "ecs/component/ui/ui_text.h"
#include "font_baker.h"
#include "io/project/roots.h"
#include "resource/asset/font_asset.h"
#include "resource/resource_manager.h"
#include "system/ui/text_layout.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;

namespace {

const FontAsset& bake(ResourceManager& resources, float pixels, const char* name = "test:roboto") {
    const Handle<FontAsset> font =
        bakeFontSDF(resources, (Roots::engineFonts() / "Roboto-Regular.ttf").string(), name, pixels);
    REQUIRE(font);
    return resources.get(font);
}

} // namespace

TEST("a UI font is baked at a height that follows the display, within its bounds") {
    const float base = UI_FONT_BASE_PIXELS;
    CHECK(uiFontPixels(0) == base);  // a display the host could not read
    CHECK(uiFontPixels(static_cast<int>(UI_FONT_REFERENCE_HEIGHT)) == base);
    CHECK(uiFontPixels(480) == base);
    CHECK_NEAR(uiFontPixels(1440), base * 1440.0f / UI_FONT_REFERENCE_HEIGHT, 1e-3f);
    CHECK(uiFontPixels(1440) > base);
    CHECK(uiFontPixels(1440) < uiFontPixels(1800));
    CHECK(uiFontPixels(8640) == UI_FONT_MAX_PIXELS);

    ResourceManager tall;
    ensureDefaultUIFont(tall, 1440);
    const Handle<FontAsset> font = tall.findByName<FontAsset>(DEFAULT_UI_FONT);
    REQUIRE(font);
    CHECK(tall.get(font).pixelHeight == uiFontPixels(1440));
    // Baked once: a second ask leaves the first.
    ensureDefaultUIFont(tall, 480);
    CHECK(tall.get(font).pixelHeight == uiFontPixels(1440));

    ResourceManager unknown;
    ensureDefaultUIFont(unknown, 0);
    CHECK(unknown.get(unknown.findByName<FontAsset>(DEFAULT_UI_FONT)).pixelHeight == base);
}

TEST("a baked font holds every glyph in a place of its own and measures text with the face's kerning") {
    ResourceManager resources;
    const FontAsset& font = bake(resources, 48.0f);
    CHECK(font.pixelHeight == 48.0f);
    REQUIRE(font.atlasSize > 0);
    CHECK(font.atlasPixels.size() == static_cast<size_t>(font.atlasSize) * font.atlasSize);
    CHECK(font.ascent > 0.0f);
    CHECK(font.descent < 0.0f);
    CHECK(font.lineHeight >= font.ascent - font.descent);

    // Each drawn glyph's cell inside the atlas, apart from every other by the mips' gutter.
    struct Cell {
        int x0, y0, x1, y1;
    };
    std::vector<Cell> cells;
    for (const char c : std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.,;:!?")) {
        const FontGlyph* glyph = font.glyph(static_cast<char32_t>(c));
        REQUIRE(glyph);
        CHECK(glyph->advance > 0.0f);
        CHECK(glyph->size.x > 0.0f);
        CHECK(glyph->uvMin.x >= 0.0f);
        CHECK(glyph->uvMax.y <= 1.0f);
        const float side = static_cast<float>(font.atlasSize);
        const auto  texel = [&](float uv) { return static_cast<int>(uv * side + 0.5f); };
        cells.push_back(
            {texel(glyph->uvMin.x), texel(glyph->uvMin.y), texel(glyph->uvMax.x), texel(glyph->uvMax.y)}
        );
        // The middle of an 'l' is inside its stroke, a corner of its cell outside.
        if (c == 'l') {
            const Cell& cell = cells.back();
            const auto at = [&](int x, int y) {
                return font.atlasPixels[static_cast<size_t>(y) * font.atlasSize + static_cast<size_t>(x)];
            };
            CHECK(at((cell.x0 + cell.x1) / 2, (cell.y0 + cell.y1) / 2) > 128);
            CHECK(at(cell.x0, cell.y0) < 128);
        }
    }
    const int gutter = 1 << (FontAsset::MIP_LEVELS - 1);
    int touching     = 0;
    for (size_t i = 0; i < cells.size(); ++i) {
        for (size_t j = i + 1; j < cells.size(); ++j) {
            const Cell& a    = cells[i];
            const Cell& b    = cells[j];
            const bool apart = a.x1 + gutter <= b.x0 || b.x1 + gutter <= a.x0 || a.y1 + gutter <= b.y0
                || b.y1 + gutter <= a.y0;
            touching += !apart;
        }
    }
    CHECK(touching == 0);

    // The face pulls A and V together; measured text is its glyphs' advances and that pull.
    const int a = FontAsset::glyphIndex(U'A');
    const int v = FontAsset::glyphIndex(U'V');
    CHECK(font.kern(a, v) < 0.0f);
    CHECK(std::is_sorted(
        font.kerning.begin(),
        font.kerning.end(),
        [](const KerningPair& l, const KerningPair& r) { return l.pair < r.pair; }
    ));
    const float apart = font.glyph(U'A')->advance + font.glyph(U'V')->advance;
    CHECK_NEAR(measureText(font, "AV", 48.0f), apart + font.kern(a, v), 1e-4f);
    CHECK_NEAR(measureText(font, "AV", 96.0f), 2.0f * measureText(font, "AV", 48.0f), 1e-3f);
    CHECK(measureText(font, "", 48.0f) == 0.0f);
}

TEST("a font baked again under its name keeps its handle, and one that will not read is said and baked never") {
    const auto root = openProject();
    ResourceManager resources;
    const std::string regular = (Roots::engineFonts() / "Roboto-Regular.ttf").string();
    const std::string medium  = (Roots::engineFonts() / "Roboto-Medium.ttf").string();
    const Handle<FontAsset> first = bakeFontSDF(resources, regular, "game:title", 32.0f);
    const Handle<FontAsset> again = bakeFontSDF(resources, medium, "game:title", 64.0f);
    CHECK(again == first);
    CHECK(resources.get(first).pixelHeight == 64.0f);

    writeText(root / "fonts/broken.ttf", "not a font at all");
    CHECK(!bakeFontSDF(resources, (root / "fonts/missing.ttf").string(), "game:missing", 32.0f));
    CHECK(!bakeFontSDF(resources, (root / "fonts/broken.ttf").string(), "game:broken", 32.0f));
    CHECK(resources.countOfType<FontAsset>() == 1);

    // A project's own list: the one that reads is baked, the one that does not is reported.
    writeText(root / "fonts/title.ttf", readText(Roots::engineFonts() / "Roboto-Medium.ttf"));
    bakeProjectFonts(resources, {{"game:menu", "fonts/title.ttf"}, {"game:sign", "fonts/broken.ttf"}}, 40.0f);
    CHECK(resources.findByName<FontAsset>("game:menu"));
    CHECK(!resources.findByName<FontAsset>("game:sign"));
    CHECK(countInLog("font 'game:sign' could not be baked") == 1);
    CHECK(countInLog("font 'game:menu'") == 0);
}
