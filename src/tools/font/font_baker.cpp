#define VKM_LOG_CATEGORY "FONT"

#include "font/font_baker.h"

#include <fstream>
#include <vector>

#include "logger.h"

#include "resource/resource_manager.h"

#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace Engine {

namespace {

// Atlas dimension: a multiple of 4 so single-channel (R8) rows stay aligned to
// GL's default 4-byte unpack alignment, and large enough for the ASCII range at
// the baked height with distance-field padding.
constexpr int           ATLAS_SIZE  = 1024;
constexpr int           SDF_PADDING = 6;    ///< Distance-field spread, in texels.
constexpr unsigned char SDF_ONEDGE  = 128;  ///< Field value on the glyph edge (0.5 normalised).

// A glyph's SDF bitmap plus stb's placement metrics, held until packing.
struct BakedGlyph {
    uint32_t       codepoint = 0;
    unsigned char* bits      = nullptr;  ///< stb-allocated SDF; freed after the copy.
    int            w = 0, h = 0;
    int            xoff = 0, yoff = 0;
    float          advance = 0.0f;
};

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    const std::streamsize size = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);
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

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0))) {
        LOG_ERROR("Font '%s' failed to parse", ttfPath.c_str());
        return {};
    }

    const float scale = stbtt_ScaleForPixelHeight(&font, pixelHeight);
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
    const float pixelDistScale = static_cast<float>(SDF_ONEDGE) / static_cast<float>(SDF_PADDING);

    // Render every printable-ASCII glyph to its own SDF bitmap.
    std::vector<BakedGlyph> baked;
    baked.reserve(FontAsset::GLYPH_COUNT);
    for (uint32_t cp = FontAsset::FIRST_CODEPOINT; cp <= FontAsset::LAST_CODEPOINT; ++cp) {
        BakedGlyph g;
        g.codepoint = cp;
        int advance = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&font, static_cast<int>(cp), &advance, &lsb);
        g.advance = advance * scale;
        // bits stays null for whitespace (e.g. space): an advance-only glyph.
        g.bits = stbtt_GetCodepointSDF(&font, scale, static_cast<int>(cp),
                                       SDF_PADDING, SDF_ONEDGE, pixelDistScale,
                                       &g.w, &g.h, &g.xoff, &g.yoff);
        baked.push_back(g);
    }

    // Pack the non-empty glyph rects into one atlas.
    std::vector<stbrp_rect> rects;
    rects.reserve(baked.size());
    for (size_t i = 0; i < baked.size(); ++i) {
        if (!baked[i].bits) continue;
        stbrp_rect r{};
        r.id = static_cast<int>(i);
        r.w  = static_cast<stbrp_coord>(baked[i].w);
        r.h  = static_cast<stbrp_coord>(baked[i].h);
        rects.push_back(r);
    }
    std::vector<stbrp_node> nodes(ATLAS_SIZE);
    stbrp_context packer;
    stbrp_init_target(&packer, ATLAS_SIZE, ATLAS_SIZE, nodes.data(), ATLAS_SIZE);
    if (!stbrp_pack_rects(&packer, rects.data(), static_cast<int>(rects.size()))) {
        // Unpacked glyphs keep their advance but render nothing; say so instead
        // of letting characters silently vanish (pixelHeight too large for the
        // fixed atlas is the usual cause).
        int unpacked = 0;
        for (const stbrp_rect& r : rects) unpacked += r.was_packed ? 0 : 1;
        LOG_WARNING("Font '%s': %d glyph(s) did not fit the %dx%d atlas at %dpx and will not render",
                    name.c_str(), unpacked, ATLAS_SIZE, ATLAS_SIZE, static_cast<int>(pixelHeight));
    }

    std::vector<const stbrp_rect*> rectForGlyph(baked.size(), nullptr);
    for (const stbrp_rect& r : rects) {
        if (r.was_packed) rectForGlyph[static_cast<size_t>(r.id)] = &r;
    }

    // Blit packed glyphs into the single-channel atlas and record metrics + uv.
    FontAsset fontAsset;
    fontAsset.atlasPixels.assign(static_cast<size_t>(ATLAS_SIZE) * ATLAS_SIZE, 0);
    fontAsset.atlasSize   = ATLAS_SIZE;
    fontAsset.pixelHeight = pixelHeight;
    fontAsset.ascent      = ascent * scale;
    fontAsset.descent     = descent * scale;
    fontAsset.lineHeight  = (ascent - descent + lineGap) * scale;

    constexpr float INV_ATLAS = 1.0f / static_cast<float>(ATLAS_SIZE);
    for (size_t i = 0; i < baked.size(); ++i) {
        BakedGlyph& g = baked[i];
        const stbrp_rect* r = rectForGlyph[i];
        FontGlyph& out = fontAsset.glyphs[g.codepoint - FontAsset::FIRST_CODEPOINT];
        out.advance = g.advance;

        if (g.bits && r) {
            for (int y = 0; y < g.h; ++y) {
                for (int x = 0; x < g.w; ++x) {
                    fontAsset.atlasPixels[(r->y + y) * ATLAS_SIZE + (r->x + x)] = g.bits[y * g.w + x];
                }
            }
            out.size   = { static_cast<float>(g.w), static_cast<float>(g.h) };
            out.offset = { static_cast<float>(g.xoff), static_cast<float>(g.yoff) };
            out.uvMin  = { r->x * INV_ATLAS, r->y * INV_ATLAS };
            out.uvMax  = { (r->x + g.w) * INV_ATLAS, (r->y + g.h) * INV_ATLAS };
        }
        if (g.bits) stbtt_FreeSDF(g.bits, nullptr);
    }

    const Handle<FontAsset> handle = resources.add(std::move(fontAsset), name);
    LOG_INFO("Baked SDF font '%s' (%u glyphs at %dpx) from '%s'",
             name.c_str(), FontAsset::GLYPH_COUNT, static_cast<int>(pixelHeight), ttfPath.c_str());
    return handle;
}

} // namespace Engine
