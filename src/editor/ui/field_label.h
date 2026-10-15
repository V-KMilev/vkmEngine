#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace Vkm::Engine {

/**
 * @brief A reflected field's name as the inspector labels it: "degreesPerSecond" -> "Degrees Per Second".
 *
 * Breaks at lower/digit-to-capital, before an acronym's last capital ("maxHPValue" ->
 * "Max HP Value") and at an underscore. The serialized key stays the raw name.
 *
 * @param name The field's name, as VKM_F records it.
 * @return The words, space-separated.
 */
inline std::string fieldLabel(std::string_view name) {
    const auto isUpper = [](char c) { return std::isupper(static_cast<unsigned char>(c)) != 0; };
    const auto isLower = [](char c) { return std::islower(static_cast<unsigned char>(c)) != 0; };
    const auto isDigit = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; };

    std::string label;
    label.reserve(name.size() + 4);
    bool wordStart = true;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (c == '_') {
            wordStart = true;
            continue;
        }
        if (i > 0 && isUpper(c)) {
            const char before = name[i - 1];
            const bool afterWord = isLower(before) || isDigit(before);
            const bool endsAcronym = isUpper(before) && i + 1 < name.size() && isLower(name[i + 1]);
            if (afterWord || endsAcronym) wordStart = true;
        }
        if (wordStart && !label.empty()) label += ' ';
        label += wordStart ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        wordStart = false;
    }
    return label;
}

/**
 * @brief Whether a vector field named @p name edits as a colour.
 *
 * The name is all a reflected field carries: a glm::vec3 then edits as RGB, a glm::vec4 as
 * RGBA.
 *
 * @param name The field's name, as VKM_F records it.
 * @return True when the name ends in "color", in any case.
 */
inline bool namesAColor(std::string_view name) {
    constexpr std::string_view COLOR = "color";
    if (name.size() < COLOR.size()) return false;
    const std::string_view tail = name.substr(name.size() - COLOR.size());
    for (std::size_t i = 0; i < COLOR.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(tail[i])) != COLOR[i]) return false;
    }
    return true;
}

} // namespace Vkm::Engine
