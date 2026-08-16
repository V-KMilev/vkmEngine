#pragma once

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Engine {

/**
 * @brief A line of text drawn within its element's rect.
 *
 * Renders `text` at `pixelSize` reference pixels, tinted by `color`, in the SDF
 * font named `font` (so it stays crisp at any size). The font is referenced by
 * asset name - not a handle - so it survives scene load (which swaps the asset
 * graph) and serializes as plain data; the UISystem resolves it through
 * ResourceManager::findByName each frame. The string is laid out as a single
 * line, aligned within the element rect on both axes; it is neither wrapped nor
 * broken across lines, so a caller wanting several lines uses several UIText
 * elements.
 */
struct UIText {
    enum class Align  : uint8_t { Left, Center, Right, Count };
    enum class VAlign : uint8_t { Top, Middle, Bottom, Count };

    std::string text;                                  ///< The string to draw (printable ASCII).
    std::string font      = "ui:roboto";               ///< Baked SDF font asset name; nothing draws if unresolved.
    float       pixelSize = 32.0f;                     ///< Text height in canvas reference pixels.
    glm::vec4   color     = {1.0f, 1.0f, 1.0f, 1.0f};  ///< Straight (non-premultiplied) RGBA.
    Align       align     = Align::Left;               ///< Horizontal alignment within the element rect.
    VAlign      valign    = VAlign::Top;               ///< Vertical alignment within the element rect.
};

} // namespace Engine

VKM_ENUM_NAMES(::Engine::UIText::Align, "Left", "Center", "Right")

VKM_ENUM_NAMES(::Engine::UIText::VAlign, "Top", "Middle", "Bottom")

VKM_REFLECT_BEGIN(::Engine::UIText)
    VKM_F(text),
    VKM_F(font),
    VKM_F(pixelSize),
    VKM_F(color),
    VKM_F(align),
    VKM_F(valign)
VKM_REFLECT_END()
