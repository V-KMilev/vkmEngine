#pragma once

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

#include "base/reflect/reflect.h"
#include "base/reflect/enum_names.h"

namespace Vkm::Engine {

inline constexpr const char* DEFAULT_UI_FONT = "ui:roboto";  ///< The font a UIText names unless told
                                                             ///< otherwise; a host with a window bakes it.

/**
 * @brief Text drawn within its element's rect, on as many lines as it breaks into.
 *
 * The font is a name the host's baked fonts are found by each frame (docs/reference/ui.md, "Fonts").
 * Text taller than the rect overflows unless an ancestor clips (clipChildren, or a UIScroll).
 */
struct UIText {
    enum class Align  : uint8_t { Left, Center, Right, Count };
    enum class VAlign : uint8_t { Top, Middle, Bottom, Count };

    static constexpr float SHADOW_DROP = 1.0f / 16.0f;  ///< How far the shadow falls, down and right, in the
                                                        ///< text's heights.

    std::string text;                                  ///< UTF-8; what the font does not cover draws nothing.
    std::string font      = DEFAULT_UI_FONT;           ///< Baked SDF font asset name; nothing draws if
                                                       ///< unresolved.
    float       pixelSize = 32.0f;                     ///< Text height in canvas reference pixels.
    glm::vec4   color     = {1.0f, 1.0f, 1.0f, 1.0f};  ///< Straight (non-premultiplied) RGBA.
    Align       align     = Align::Left;               ///< Horizontal alignment within the element rect.
    VAlign      valign    = VAlign::Top;               ///< Vertical alignment of the whole block within the
                                                       ///< element rect, which does not bound it: clip above
                                                       ///< it if it may overflow.
    glm::vec4   shadow    = {0.0f, 0.0f, 0.0f, 0.0f};  ///< A drop shadow under the text, down and right by
                                                       ///< SHADOW_DROP of its height, so it reads over the
                                                       ///< scene without a panel behind it; none while
                                                       ///< transparent.

    bool wrap = false;  ///< Whether a line too wide for the element rect breaks onto the next. It breaks at
                        ///< the last space that fits, or mid-word for a word wider than the rect. A newline
                        ///< always breaks; walkGlyphs is the one line walk.
};

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::UIText::Align, "Left", "Center", "Right")

VKM_ENUM_NAMES(::Vkm::Engine::UIText::VAlign, "Top", "Middle", "Bottom")

VKM_REFLECT_BEGIN(::Vkm::Engine::UIText)
    VKM_F(text)
    VKM_F(font, "The baked font it is drawn with")
    VKM_F(pixelSize, 1.0f, 512.0f, "Text height in reference pixels")
    VKM_F(color)
    VKM_F(align)
    VKM_F(valign)
    VKM_F(shadow, "A drop shadow, so it reads over the scene; none while clear")
    VKM_F(wrap, "Breaks a line too wide for the element")
VKM_REFLECT_END()
