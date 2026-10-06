#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "resource/resource.h"
#include "resource/resource_handle.h"

namespace Vkm::Engine {

/**
 * @brief One glyph's atlas placement and layout metrics, in baked pixels.
 *
 * Scale by requestedSize / pixelHeight. uvMin / uvMax address the glyph's cell
 * in the SDF atlas (0..1, top-left origin).
 */
struct FontGlyph {
    glm::vec2 uvMin   = {0.0f, 0.0f};
    glm::vec2 uvMax   = {0.0f, 0.0f};
    glm::vec2 size    = {0.0f, 0.0f};  ///< Glyph quad size in baked pixels.
    /// Quad top-left relative to the pen on the baseline (negative y is above it).
    glm::vec2 offset  = {0.0f, 0.0f};
    float     advance = 0.0f;          ///< Pen advance in baked pixels.
};

/**
 * @brief How far the pen moves between one glyph and the next, past their advances.
 *
 * Packed as left * FontAsset::GLYPH_COUNT + right, by glyph index, so a sorted
 * table answers a pair with one binary search.
 */
struct KerningPair {
    uint32_t pair   = 0;
    float    adjust = 0.0f;  ///< Added to the left glyph's advance, in baked pixels; negative pulls together.
};

/**
 * @brief A TrueType font baked to a signed-distance-field atlas.
 *
 * Covers printable ASCII, printable Latin-1 and MARKS; one bake renders cleanly
 * to roughly three times its baked height. Self-contained (pixels, no texture
 * handle), so ResourceManager::swap can keep the slot across a scene load.
 */
struct FontAsset : public Resource {
    static constexpr uint32_t ASCII_FIRST  = 0x20;  ///< Space.
    static constexpr uint32_t ASCII_LAST   = 0x7E;  ///< '~'.
    static constexpr uint32_t LATIN1_FIRST = 0xA0;  ///< No-break space.
    static constexpr uint32_t LATIN1_LAST  = 0xFF;  ///< y with diaeresis.

    /// Dashes, curly quotes, the bullet, the ellipsis and the euro sign, ascending.
    static constexpr uint32_t MARKS[] = {
        0x2013,
        0x2014,
        0x2018,
        0x2019,
        0x201C,
        0x201D,
        0x2022,
        0x2026,
        0x20AC
    };

    static constexpr uint32_t ASCII_COUNT  = ASCII_LAST - ASCII_FIRST + 1;
    static constexpr uint32_t LATIN1_COUNT = LATIN1_LAST - LATIN1_FIRST + 1;
    static constexpr uint32_t MARK_COUNT   = sizeof(MARKS) / sizeof(MARKS[0]);
    static constexpr uint32_t GLYPH_COUNT  = ASCII_COUNT + LATIN1_COUNT + MARK_COUNT;

    /**
     * @brief How many levels of the atlas a renderer may sample, the full size included.
     *
     * bakeFontSDF's gutter is 2^(MIP_LEVELS - 1) texels; more levels would bleed
     * neighbouring glyphs together.
     */
    static constexpr uint32_t MIP_LEVELS = 4;

    std::vector<uint8_t> atlasPixels;   ///< Single-channel SDF atlas, atlasSize x atlasSize texels.
    uint32_t atlasSize   = 0;           ///< Atlas dimension in texels (square).

    float pixelHeight = 0.0f;
    float ascent      = 0.0f;           ///< Baseline-to-top, in baked pixels (positive).
    /// Baseline-to-bottom, in baked pixels (negative below the baseline).
    float descent     = 0.0f;

    /**
     * @brief Baseline-to-baseline distance for stacked lines, in baked pixels.
     *
     * The face's own ascent - descent + line gap.
     */
    float lineHeight  = 0.0f;

    std::array<FontGlyph, GLYPH_COUNT> glyphs{};

    /// Sorted on KerningPair::pair; unkerned pairs are absent.
    std::vector<KerningPair> kerning;

    /**
     * @brief Where @p codepoint sits in the glyph table.
     *
     * @param codepoint The character.
     * @return Its index, or -1 when a bake does not cover it.
     */
    static constexpr int glyphIndex(char32_t codepoint) {
        if (codepoint >= ASCII_FIRST && codepoint <= ASCII_LAST) {
            return static_cast<int>(codepoint - ASCII_FIRST);
        }
        if (codepoint >= LATIN1_FIRST && codepoint <= LATIN1_LAST) {
            return static_cast<int>(ASCII_COUNT + codepoint - LATIN1_FIRST);
        }
        for (uint32_t i = 0; i < MARK_COUNT; ++i) {
            if (MARKS[i] == codepoint) return static_cast<int>(ASCII_COUNT + LATIN1_COUNT + i);
        }
        return -1;
    }

    /**
     * @brief The codepoint glyph @p index holds; the inverse of glyphIndex.
     *
     * @param index Glyph table index, below GLYPH_COUNT.
     * @return The character at that index.
     */
    static constexpr char32_t codepointAt(uint32_t index) {
        if (index < ASCII_COUNT) return ASCII_FIRST + index;
        if (index < ASCII_COUNT + LATIN1_COUNT) return LATIN1_FIRST + index - ASCII_COUNT;
        return MARKS[index - ASCII_COUNT - LATIN1_COUNT];
    }

    /**
     * @brief The glyph for @p codepoint.
     *
     * @param codepoint The character.
     * @return Its glyph, or nullptr when a bake does not cover it.
     */
    const FontGlyph* glyph(char32_t codepoint) const {
        const int index = glyphIndex(codepoint);
        return index < 0 ? nullptr : &glyphs[static_cast<size_t>(index)];
    }

    /**
     * @brief The kerning between glyph @p left and glyph @p right, in baked pixels.
     *
     * @param left  Glyph index of the earlier glyph.
     * @param right Glyph index of the one after it.
     * @return The adjustment to the left glyph's advance; zero for a pair the
     *         face does not kern.
     */
    float kern(int left, int right) const {
        const uint32_t pair = static_cast<uint32_t>(left) * GLYPH_COUNT + static_cast<uint32_t>(right);
        const auto found = std::lower_bound(
            kerning.begin(),
            kerning.end(),
            pair,
            [](const KerningPair& entry, uint32_t key) { return entry.pair < key; }
        );
        return (found != kerning.end() && found->pair == pair) ? found->adjust : 0.0f;
    }
};

using FontHandle = Handle<FontAsset>;

} // namespace Vkm::Engine
