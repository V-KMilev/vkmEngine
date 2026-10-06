#define VKM_LOG_CATEGORY "FONT"

#include "loader/font_baker.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <vector>

#include "logger.h"

#include "platform/threading/thread_pool.h"
#include "resource/resource_manager.h"

#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace Vkm::Engine {

namespace {

constexpr unsigned char SDF_ONEDGE = 128;  ///< Field value on the glyph edge (0.5 normalised).

// Texels between glyphs: what the smallest mip level averages into one, so none mixes two glyphs.
constexpr int GUTTER = 1 << (FontAsset::MIP_LEVELS - 1);

// The largest atlas a bake makes; half GL 4.3's minimum GL_MAX_TEXTURE_SIZE.
constexpr int MAX_ATLAS = 8192;

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
    int            glyph = 0;            ///< stb's glyph index; 0 where the face has none.
    unsigned char* bits  = nullptr;      ///< stb-allocated SDF; freed after the copy.
    int            w = 0, h = 0;
    int            xoff = 0, yoff = 0;
    float          advance = 0.0f;
};

/**
 * @brief Pack @p rects into the smallest square atlas that holds them all.
 *
 * Doubles from the power of two whose area covers the rects' until every one fits.
 *
 * @param rects Glyph rects, gutter included; their placement is written back.
 * @return The atlas dimension, a power of two of at least 256 (a multiple of four, as GL's
 *         default unpack alignment needs). MAX_ATLAS when even that does not hold them, with
 *         the rects that did not fit left unpacked.
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

    // stbtt_GetFontOffsetForIndex reads past the end of a buffer shorter than the offset table.
    constexpr size_t OFFSET_TABLE_BYTES = 12;
    if (ttf.size() < OFFSET_TABLE_BYTES) {
        LOG_ERROR("Font '%s' is %zu bytes, too short to be a font file", ttfPath.c_str(), ttf.size());
        return {};
    }

    // -1 for a non-font, which stbtt_InitFont would take as an offset and read through.
    const int offset = stbtt_GetFontOffsetForIndex(ttf.data(), 0);
    if (offset < 0) {
        LOG_ERROR("Font '%s' is not a font file", ttfPath.c_str());
        return {};
    }

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, ttf.data(), offset)) {
        LOG_ERROR("Font '%s' failed to parse", ttfPath.c_str());
        return {};
    }

    const float scale = stbtt_ScaleForPixelHeight(&font, pixelHeight);
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
    const int   padding        = sdfPadding(pixelHeight);
    const float pixelDistScale = static_cast<float>(SDF_ONEDGE) / static_cast<float>(padding);

    // Across the pool: the field is the whole cost, over a second at 4K serially. Each glyph
    // writes its own slot, and stb_truetype only reads the face.
    std::vector<BakedGlyph> baked(FontAsset::GLYPH_COUNT);
    parallelFor(FontAsset::GLYPH_COUNT, 8, [&](size_t i) {
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

    // Each bitmap already carries `padding` texels of field; the gutter is for the mip levels.
    std::vector<stbrp_rect> rects;
    rects.reserve(baked.size());
    for (size_t i = 0; i < baked.size(); ++i) {
        if (!baked[i].bits) continue;
        stbrp_rect r{};
        r.id = static_cast<int>(i);
        r.w  = static_cast<stbrp_coord>(baked[i].w + GUTTER);
        r.h  = static_cast<stbrp_coord>(baked[i].h + GUTTER);
        rects.push_back(r);
    }
    const int atlasSize = packAtlas(rects);

    std::vector<const stbrp_rect*> rectForGlyph(baked.size(), nullptr);
    int unpacked = 0;
    for (const stbrp_rect& r : rects) {
        if (r.was_packed) rectForGlyph[static_cast<size_t>(r.id)] = &r;
        else              ++unpacked;
    }
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

    FontAsset fontAsset;
    fontAsset.atlasPixels.assign(static_cast<size_t>(atlasSize) * atlasSize, 0);
    fontAsset.atlasSize   = static_cast<uint32_t>(atlasSize);
    fontAsset.pixelHeight = pixelHeight;
    fontAsset.ascent      = ascent * scale;
    fontAsset.descent     = descent * scale;
    fontAsset.lineHeight  = (ascent - descent + lineGap) * scale;

    const float invAtlas = 1.0f / static_cast<float>(atlasSize);
    for (size_t i = 0; i < baked.size(); ++i) {
        BakedGlyph& g = baked[i];
        const stbrp_rect* r = rectForGlyph[i];
        FontGlyph& out = fontAsset.glyphs[i];
        out.advance = g.advance;

        if (g.bits && r) {
            for (int y = 0; y < g.h; ++y) {
                for (int x = 0; x < g.w; ++x) {
                    fontAsset.atlasPixels[(r->y + y) * atlasSize + (r->x + x)] = g.bits[y * g.w + x];
                }
            }
            out.size   = { static_cast<float>(g.w), static_cast<float>(g.h) };
            out.offset = { static_cast<float>(g.xoff), static_cast<float>(g.yoff) };
            out.uvMin  = { r->x * invAtlas, r->y * invAtlas };
            out.uvMax  = { (r->x + g.w) * invAtlas, (r->y + g.h) * invAtlas };
        }
        if (g.bits) stbtt_FreeSDF(g.bits, nullptr);
    }

    // Left outer, so the table comes out sorted on its key.
    for (uint32_t left = 0; left < FontAsset::GLYPH_COUNT; ++left) {
        if (baked[left].glyph == 0) continue;
        for (uint32_t right = 0; right < FontAsset::GLYPH_COUNT; ++right) {
            if (baked[right].glyph == 0) continue;
            const int adjust = stbtt_GetGlyphKernAdvance(&font, baked[left].glyph, baked[right].glyph);
            if (adjust == 0) continue;
            fontAsset.kerning.push_back(KerningPair{left * FontAsset::GLYPH_COUNT + right, adjust * scale});
        }
    }

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

} // namespace Vkm::Engine
