#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace Vkm::Engine::Utf8 {

/// What a malformed sequence decodes as, and what an unencodable codepoint encodes as.
constexpr char32_t REPLACEMENT = 0xFFFD;

/**
 * @brief Decode the codepoint that starts at @p at, and step past it.
 *
 * A malformed sequence decodes as REPLACEMENT and consumes one byte, so a walk
 * always advances and resynchronises on the next lead byte.
 *
 * @param text Text being walked; @p at must be inside it.
 * @param at   Byte offset of the sequence; left at the byte after it.
 * @return The codepoint.
 */
inline char32_t next(std::string_view text, std::size_t& at) {
    const unsigned char lead = static_cast<unsigned char>(text[at]);
    if (lead < 0x80) {
        ++at;
        return lead;
    }

    std::size_t length = 0;
    char32_t    value  = 0;
    char32_t    least  = 0;   // the smallest codepoint this length may carry
    if ((lead & 0xE0) == 0xC0) {
        length = 2;
        value = lead & 0x1F;
        least = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
        length = 3;
        value = lead & 0x0F;
        least = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
        length = 4;
        value = lead & 0x07;
        least = 0x10000;
    } else {
        ++at;
        return REPLACEMENT;
    }

    if (at + length > text.size()) {
        ++at;
        return REPLACEMENT;
    }
    for (std::size_t i = 1; i < length; ++i) {
        const unsigned char byte = static_cast<unsigned char>(text[at + i]);
        if ((byte & 0xC0) != 0x80) {
            ++at;
            return REPLACEMENT;
        }
        value = (value << 6) | (byte & 0x3F);
    }
    if (value < least || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
        ++at;
        return REPLACEMENT;
    }
    at += length;
    return value;
}

/**
 * @brief Append @p codepoint to @p text as UTF-8.
 *
 * @param text      String to extend.
 * @param codepoint Codepoint to encode; a surrogate or one past U+10FFFF
 *                  becomes REPLACEMENT.
 */
inline void append(std::string& text, char32_t codepoint) {
    if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) codepoint = REPLACEMENT;

    if (codepoint < 0x80) {
        text.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800) {
        text.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint < 0x10000) {
        text.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        text.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

/**
 * @brief Where the codepoint that ends at @p at begins.
 *
 * Agrees with next() on malformed text: a byte next() steps over alone is
 * stepped back over alone.
 *
 * @param text Text being edited.
 * @param at   Byte offset just past the codepoint; 0 has nothing before it.
 * @return Its first byte's offset, or 0 when @p at is 0.
 */
inline std::size_t previous(std::string_view text, std::size_t at) {
    if (at == 0) return 0;
    if (at > text.size()) at = text.size();
    // At most three continuation bytes precede a lead byte.
    std::size_t start = at - 1;
    for (int step = 0; step < 3 && start > 0; ++step) {
        if ((static_cast<unsigned char>(text[start]) & 0xC0) != 0x80) break;
        --start;
    }
    std::size_t end = start;
    next(text, end);
    return end == at ? start : at - 1;
}

} // namespace Vkm::Engine::Utf8
