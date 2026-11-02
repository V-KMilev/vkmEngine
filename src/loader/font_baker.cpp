#define VKM_LOG_CATEGORY "FONT"

#include "font_baker.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <vector>

#include "vkm_log/logger.h"

#include "debug/error/engine_error_log.h"
#include "ecs/component/ui/ui_text.h"
#include "io/project/roots.h"
#include "vkm_platform/threading/thread_pool.h"
#include "resource/resource_manager.h"
#include "system/render/render_backend.h"

#include "stb_rect_pack.h"
#include "stb_truetype.h"

namespace Vkm::Engine {

namespace {

constexpr unsigned char SDF_ONEDGE = 128;  ///< Field value on the glyph edge (0.5 normalized).

// Texels between glyphs: what the smallest mip level averages into one, so none mixes two glyphs.
constexpr int GUTTER = 1 << (FontAsset::MIP_LEVELS - 1);

// The largest atlas a bake makes: half the edge every backend takes.
constexpr int MAX_ATLAS = static_cast<int>(BACKEND_MIN_TEXTURE_EDGE / 2);

/**
 * @brief Distance-field spread for a bake of @p pixelHeight, in texels.
 *
 * Proportional, so a taller bake keeps the anti-aliasing's relative width. Never narrower than
 * the gutter: a field clamped across what the smallest mip averages thins the strokes.
 * pixelDistScale is derived from it, so the encoding is consistent at any spread.
 *
 * @param pixelHeight Glyph height being baked.
 * @return Padding in texels.
 */
int sdfPadding(float pixelHeight) {
    return std::max(GUTTER, static_cast<int>(pixelHeight * 0.09f + 0.5f));
}

// A glyph's SDF bitmap plus stb's placement metrics, held until packing.
struct BakedGlyph {
    int            glyph   = 0;        ///< stb's glyph index; 0 where the face has none.
    unsigned char* bits    = nullptr;  ///< stb-allocated SDF; freed after the copy.
    int            w       = 0, h = 0;
    int            xoff    = 0, yoff = 0;
    float          advance = 0.0f;
};

/**
 * @brief Pack @p rects into the smallest square atlas that holds them all.
 *
 * Doubles from the power of two whose area covers the rects' until every one fits.
 *
 * @param rects Glyph rects, gutter included; their placement is written back.
 * @return The atlas dimension, a power of two of at least 256 (a multiple of four, so rows of
 *         single bytes stay four-byte aligned). MAX_ATLAS when even that does not hold them,
 *         with the rects that did not fit left unpacked.
 */
int packAtlas(std::vector<stbrp_rect>& rects) {
    int64_t area = 0;
    for (const stbrp_rect& r : rects) area += static_cast<int64_t>(r.w) * r.h;

    int size = 256;
    while (size < MAX_ATLAS && static_cast<int64_t>(size) * size < area) size *= 2;

    for (;; size *= 2) {
        std::vector<stbrp_node> nodes(static_cast<size_t>(size));
        stbrp_context packer;
        stbrp_init_target(&packer, size, size, nodes.data(), size);
        if (stbrp_pack_rects(&packer, rects.data(), static_cast<int>(rects.size())) || size >= MAX_ATLAS) {
            return size;
        }
    }
}

// Empty for anything not a whole file read. A negative tellg must be caught here: widened to
// size_t it asks for every byte the machine has.
std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    const std::streamsize size = file.tellg();
    if (size <= 0) return {};
    file.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) return {};
    return data;
}

/**
 * @brief Open the first face of a font file's bytes, refusing what stb_truetype would read past.
 *
 * @param ttf  The file's bytes; the face reads them, so they outlive it.
 * @param path The file, for the log.
 * @param font Filled on success.
 * @return False, having logged, for bytes that are no font.
 */
bool openFace(const std::vector<uint8_t>& ttf, const std::string& path, stbtt_fontinfo& font) {
    // stbtt_GetFontOffsetForIndex reads past the end of a buffer shorter than the offset table.
    constexpr size_t OFFSET_TABLE_BYTES = 12;
    if (ttf.size() < OFFSET_TABLE_BYTES) {
        LOG_ERROR("Font '%s' is %zu bytes, too short to be a font file", path.c_str(), ttf.size());
        return false;
    }

    // -1 for a non-font, which stbtt_InitFont would take as an offset and read through.
    const int offset = stbtt_GetFontOffsetForIndex(ttf.data(), 0);
    if (offset < 0) {
        LOG_ERROR("Font '%s' is not a font file", path.c_str());
        return false;
    }

    if (!stbtt_InitFont(&font, ttf.data(), offset)) {
        LOG_ERROR("Font '%s' failed to parse", path.c_str());
        return false;
    }
    return true;
}

/**
 * @brief Render every glyph FontAsset holds as a distance field.
 *
 * Across the pool: the field is the whole cost, over a second at 4K serially. Each glyph
 * writes its own slot, and stb_truetype only reads the face.
 *
 * @param font    The face.
 * @param scale   Font units to baked pixels.
 * @param padding sdfPadding's spread.
 * @return One per FontAsset slot; the caller frees each field with stbtt_FreeSDF.
 */
std::vector<BakedGlyph> renderGlyphs(const stbtt_fontinfo& font, float scale, int padding) {
    const float pixelDistScale = static_cast<float>(SDF_ONEDGE) / static_cast<float>(padding);

    std::vector<BakedGlyph> baked(FontAsset::GLYPH_COUNT);
    Platform::parallelFor(FontAsset::GLYPH_COUNT, 8, [&](size_t i) {
        BakedGlyph& g = baked[i];
        const char32_t codepoint = FontAsset::codepointAt(static_cast<uint32_t>(i));
        g.glyph = stbtt_FindGlyphIndex(&font, static_cast<int>(codepoint));
        // A missing codepoint draws and moves nothing, not the face's missing-glyph box.
        if (g.glyph == 0) return;

        int advance = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&font, g.glyph, &advance, &lsb);
        g.advance = advance * scale;
        // Null for a glyph with no outline (whitespace); it keeps its advance.
        g.bits = stbtt_GetGlyphSDF(
            &font,
            scale,
            g.glyph,
            padding,
            SDF_ONEDGE,
            pixelDistScale,
            &g.w,
            &g.h,
            &g.xoff,
            &g.yoff
        );
    });
    return baked;
}

/**
 * @brief Pack the outlined glyphs into one square atlas.
 *
 * Each bitmap already carries `padding` texels of field; the gutter is for the mip levels.
 *
 * @param baked The glyphs; one with no outline takes no space.
 * @param rects Filled with a rect per outlined glyph, its id the glyph's slot.
 * @return packAtlas's edge.
 */
int packGlyphs(const std::vector<BakedGlyph>& baked, std::vector<stbrp_rect>& rects) {
    rects.clear();
    rects.reserve(baked.size());
    for (size_t i = 0; i < baked.size(); ++i) {
        if (!baked[i].bits) continue;
        stbrp_rect r{};
        r.id = static_cast<int>(i);
        r.w  = static_cast<stbrp_coord>(baked[i].w + GUTTER);
        r.h  = static_cast<stbrp_coord>(baked[i].h + GUTTER);
        rects.push_back(r);
    }
    return packAtlas(rects);
}

/**
 * @brief Copy each packed glyph's field into @p out's atlas, its placement into its glyph.
 *
 * Frees every field, packed or not.
 *
 * @param baked     The rendered glyphs; their fields are freed.
 * @param rects     packGlyphs's rects.
 * @param atlasSize packGlyphs's edge; @p out's atlas is allocated at it.
 * @param out       Takes the atlas and the glyphs.
 * @return How many outlined glyphs did not fit, which will not render.
 */
int fillAtlas(
    std::vector<BakedGlyph>& baked,
    const std::vector<stbrp_rect>& rects,
    int atlasSize,
    FontAsset& out
) {
    std::vector<const stbrp_rect*> rectForGlyph(baked.size(), nullptr);
    int unpacked = 0;
    for (const stbrp_rect& r : rects) {
        if (r.was_packed) rectForGlyph[static_cast<size_t>(r.id)] = &r;
        else              ++unpacked;
    }

    out.atlasPixels.assign(static_cast<size_t>(atlasSize) * atlasSize, 0);
    out.atlasSize = static_cast<uint32_t>(atlasSize);

    const float invAtlas = 1.0f / static_cast<float>(atlasSize);
    for (size_t i = 0; i < baked.size(); ++i) {
        BakedGlyph& g = baked[i];
        const stbrp_rect* r = rectForGlyph[i];
        FontGlyph& glyph = out.glyphs[i];
        glyph.advance = g.advance;

        if (g.bits && r) {
            for (int y = 0; y < g.h; ++y) {
                for (int x = 0; x < g.w; ++x) {
                    out.atlasPixels[(r->y + y) * atlasSize + (r->x + x)] = g.bits[y * g.w + x];
                }
            }
            glyph.size   = { static_cast<float>(g.w), static_cast<float>(g.h) };
            glyph.offset = { static_cast<float>(g.xoff), static_cast<float>(g.yoff) };
            glyph.uvMin  = { r->x * invAtlas, r->y * invAtlas };
            glyph.uvMax  = { (r->x + g.w) * invAtlas, (r->y + g.h) * invAtlas };
        }
        if (g.bits) stbtt_FreeSDF(g.bits, nullptr);
    }
    return unpacked;
}

/**
 * @brief The kerning between every two glyphs the face has, sorted on its key.
 *
 * @param font  The face.
 * @param baked The glyphs, by FontAsset slot.
 * @param scale Font units to baked pixels.
 * @return Only the pairs that move.
 */
std::vector<KerningPair> kerningOf(
    const stbtt_fontinfo& font,
    const std::vector<BakedGlyph>& baked,
    float scale
) {
    std::vector<KerningPair> pairs;
    // Left outer, so the table comes out sorted on its key.
    for (uint32_t left = 0; left < FontAsset::GLYPH_COUNT; ++left) {
        if (baked[left].glyph == 0) continue;
        for (uint32_t right = 0; right < FontAsset::GLYPH_COUNT; ++right) {
            if (baked[right].glyph == 0) continue;
            const int adjust = stbtt_GetGlyphKernAdvance(&font, baked[left].glyph, baked[right].glyph);
            if (adjust == 0) continue;
            pairs.push_back(KerningPair{left * FontAsset::GLYPH_COUNT + right, adjust * scale});
        }
    }
    return pairs;
}

} // namespace

Handle<FontAsset> bakeFontSDF(
    ResourceManager& resources,
    const std::string& ttfPath,
    const std::string& name,
    float pixelHeight
) {
    const std::vector<uint8_t> ttf = readFile(ttfPath);
    if (ttf.empty()) {
        LOG_ERROR("Font '%s' could not be read", ttfPath.c_str());
        return {};
    }
    stbtt_fontinfo font;
    if (!openFace(ttf, ttfPath, font)) return {};

    const float scale = stbtt_ScaleForPixelHeight(&font, pixelHeight);
    std::vector<BakedGlyph> baked = renderGlyphs(font, scale, sdfPadding(pixelHeight));

    std::vector<stbrp_rect> rects;
    const int atlasSize = packGlyphs(baked, rects);
    FontAsset fontAsset;
    const int unpacked = fillAtlas(baked, rects, atlasSize, fontAsset);
    if (unpacked > 0) {
        LOG_WARNING(
            "Font '%s': %d glyph(s) did not fit a %dx%d atlas at %dpx and will not render",
            name.c_str(),
            unpacked,
            atlasSize,
            atlasSize,
            static_cast<int>(pixelHeight)
        );
    }

    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
    fontAsset.pixelHeight = pixelHeight;
    fontAsset.ascent      = ascent * scale;
    fontAsset.descent     = descent * scale;
    fontAsset.lineHeight  = (ascent - descent + lineGap) * scale;
    fontAsset.kerning     = kerningOf(font, baked, scale);

    // A name already held takes the new bake in place, so a handle given out reads it.
    const size_t pairs = fontAsset.kerning.size();
    const Handle<FontAsset> handle = resources.add(std::move(fontAsset), name);
    LOG_INFO(
        "Baked SDF font '%s' (%u glyphs, %zu kerning pairs, %dx%d atlas at %dpx) from '%s'",
        name.c_str(),
        FontAsset::GLYPH_COUNT,
        pairs,
        atlasSize,
        atlasSize,
        static_cast<int>(pixelHeight),
        ttfPath.c_str()
    );
    return handle;
}

float uiFontPixels(int displayHeight) {
    const float share  = static_cast<float>(displayHeight) / UI_FONT_REFERENCE_HEIGHT;
    const float wanted = displayHeight > 0 ? UI_FONT_BASE_PIXELS * share : UI_FONT_BASE_PIXELS;
    return std::clamp(wanted, UI_FONT_BASE_PIXELS, UI_FONT_MAX_PIXELS);
}

void bakeProjectFonts(ResourceManager& resources, const std::vector<FontEntry>& fonts, float pixelHeight) {
    for (const FontEntry& font : fonts) {
        const std::string path = Roots::resolveProjectPath(font.file).string();
        if (bakeFontSDF(resources, path, font.name, pixelHeight)) continue;
        const std::string failed = "font '" + font.name + "' could not be baked";
        reportError("Project", font.file, failed + "; its text draws none, or its last bake");
    }
}

void ensureDefaultUIFont(ResourceManager& resources, int displayHeight) {
    if (resources.findByName<FontAsset>(DEFAULT_UI_FONT)) return;
    bakeFontSDF(
        resources,
        (Roots::engineFonts() / "Roboto-Medium.ttf").string(),
        DEFAULT_UI_FONT,
        uiFontPixels(displayHeight)
    );
}

} // namespace Vkm::Engine
