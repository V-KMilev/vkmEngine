#pragma once

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief The font a UIText names unless told otherwise; a host with a window bakes it.
 */
inline constexpr const char* DEFAULT_UI_FONT = "ui:roboto";

/**
 * @brief Text drawn within its element's rect, on as many lines as it breaks into.
 *
 * The font is a name, not a handle, because a name is what a scene file can hold;
 * it is resolved through ResourceManager::findByName each frame. Text taller than
 * the rect overflows unless an ancestor clips (clipChildren, or a UIScroll).
 */
struct UIText {
    enum class Align  : uint8_t { Left, Center, Right, Count };
    enum class VAlign : uint8_t { Top, Middle, Bottom, Count };

    /// UTF-8; what the font does not cover draws nothing.
    std::string text;
    /// Baked SDF font asset name; nothing draws if unresolved.
    std::string font      = DEFAULT_UI_FONT;
    float       pixelSize = 32.0f;                     ///< Text height in canvas reference pixels.
    glm::vec4   color     = {1.0f, 1.0f, 1.0f, 1.0f};  ///< Straight (non-premultiplied) RGBA.
    Align       align     = Align::Left;               ///< Horizontal alignment within the element rect.
    VAlign      valign    = VAlign::Top;               ///< Vertical alignment within the element rect.

    /**
     * @brief Whether a line too wide for the element rect breaks onto the next.
     */
    bool wrap = false;
};

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::UIText::Align, "Left", "Center", "Right")

VKM_ENUM_NAMES(::Vkm::Engine::UIText::VAlign, "Top", "Middle", "Bottom")

VKM_REFLECT_BEGIN(::Vkm::Engine::UIText)
    VKM_F(text)
    VKM_F(font)
    VKM_F(pixelSize)
    VKM_F(color)
    VKM_F(align)
    VKM_F(valign)
    VKM_F(wrap)
VKM_REFLECT_END()
