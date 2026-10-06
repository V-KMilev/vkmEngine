#pragma once

#include <cstddef>
#include <string_view>

#include "core/utf8.h"
#include "resource/asset/font_asset.h"

namespace Vkm::Engine {

/**
 * @brief Walk one line of UTF-8 @p text glyph by glyph, kerned, from a pen at zero.
 *
 * Both measuring and drawing use it, so they cannot disagree. An uncovered codepoint
 * moves nothing and breaks kerning across it.
 *
 * @tparam Visit Callable as `bool(std::size_t, const FontGlyph&, float)`.
 * @param font  Font whose glyphs and kerning lay the line out.
 * @param text  One line; a newline in it is a codepoint like any other.
 * @param scale Baked pixels to output units for this text.
 * @param visit Called as `visit(byteOffset, glyph, penX)` for each covered
 *              codepoint, penX being where that glyph's origin sits; it
 *              returns false to stop the walk before the glyph is advanced past.
 * @return The pen after the last glyph walked past: the line's width when the
 *         walk was not stopped.
 */
template <typename Visit>
float walkGlyphs(const FontAsset& font, std::string_view text, float scale, Visit&& visit) {
    float       pen      = 0.0f;
    int         previous = -1;
    std::size_t at       = 0;
    while (at < text.size()) {
        const std::size_t start = at;
        const int index = FontAsset::glyphIndex(Utf8::next(text, at));
        if (index < 0) {
            previous = -1;
            continue;
        }
        if (previous >= 0) pen += font.kern(previous, index) * scale;

        const FontGlyph& glyph = font.glyphs[static_cast<std::size_t>(index)];
        if (!visit(start, glyph, pen)) return pen;
        pen     += glyph.advance * scale;
        previous = index;
    }
    return pen;
}

/**
 * @brief How wide @p text draws on one line at @p pixelSize; the width layout uses.
 *
 * @param font      The font the UIText names.
 * @param text      One line of UTF-8.
 * @param pixelSize UIText::pixelSize; the width comes back in the same units.
 * @return The width, in the units @p pixelSize is in.
 */
inline float measureText(const FontAsset& font, std::string_view text, float pixelSize) {
    if (font.pixelHeight <= 0.0f) return 0.0f;
    return walkGlyphs(
        font,
        text,
        pixelSize / font.pixelHeight,
        [](std::size_t, const FontGlyph&, float) { return true; }
    );
}

} // namespace Vkm::Engine
