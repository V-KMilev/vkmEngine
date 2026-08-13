#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "resource/resource.h"
#include "resource/resource_handle.h"

namespace Engine {

/**
 * @brief One glyph's atlas placement and layout metrics, in baked pixels.
 *
 * Sizes and offsets are expressed at the font's baked pixelHeight; the UISystem
 * scales them to the requested text size at layout time. uvMin / uvMax address
 * the glyph's cell in the SDF atlas (0..1, top-left origin).
 */
struct FontGlyph {
    glm::vec2 uvMin   = {0.0f, 0.0f};
    glm::vec2 uvMax   = {0.0f, 0.0f};
    glm::vec2 size    = {0.0f, 0.0f};  ///< Glyph quad size in baked pixels.
    glm::vec2 offset  = {0.0f, 0.0f};  ///< Quad top-left relative to the pen on the baseline (negative y is above it).
    float     advance = 0.0f;          ///< Pen advance in baked pixels.
};

/**
 * @brief A TrueType font baked to a signed-distance-field atlas.
 *
 * Holds the single-channel (R8) SDF atlas pixels and per-glyph metrics for the
 * printable-ASCII range. Because the atlas stores signed distance, one bake
 * renders crisply at any text size: the UISystem just scales the metrics by
 * requestedSize / pixelHeight.
 *
 * The asset is deliberately self-contained - the atlas lives here as pixels,
 * not as a handle into the texture slot. Fonts are runtime-baked and never
 * enter scene files, so scene load carries the FontAsset slot across the asset
 * graph swap (the same treatment shaders get); that is only safe because
 * nothing in here can dangle. The backend uploads the atlas from these pixels
 * like any other resource, keyed by FontHandle.
 */
struct FontAsset : public Resource {
    static constexpr uint32_t FIRST_CODEPOINT = 32;   ///< Space.
    static constexpr uint32_t LAST_CODEPOINT  = 126;  ///< '~'.
    static constexpr uint32_t GLYPH_COUNT     = LAST_CODEPOINT - FIRST_CODEPOINT + 1;

    std::vector<uint8_t> atlasPixels;   ///< Single-channel SDF atlas, atlasSize x atlasSize texels.
    uint32_t atlasSize   = 0;           ///< Atlas dimension in texels (square).

    float pixelHeight = 0.0f;           ///< Pixel height the metrics were baked at.
    float ascent      = 0.0f;           ///< Baseline-to-top, in baked pixels (positive).
    float descent     = 0.0f;           ///< Baseline-to-bottom, in baked pixels (negative below the baseline).
    float lineHeight  = 0.0f;           ///< Recommended line advance, in baked pixels.

    std::array<FontGlyph, GLYPH_COUNT> glyphs{};

    /**
     * @brief The glyph for @p codepoint, or nullptr when it is outside the
     *        baked printable-ASCII range.
     */
    const FontGlyph* glyph(uint32_t codepoint) const {
        if (codepoint < FIRST_CODEPOINT || codepoint > LAST_CODEPOINT) return nullptr;
        return &glyphs[codepoint - FIRST_CODEPOINT];
    }
};

using FontHandle = Handle<FontAsset>;

} // namespace Engine
