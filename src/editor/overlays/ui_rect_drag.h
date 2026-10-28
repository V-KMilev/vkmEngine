#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

#include "ecs/component/ui/ui_element.h"

namespace Vkm::Engine {

/**
 * @brief The part of a UI element's rect a drag holds.
 */
enum class RectHandle : uint8_t {
    None,
    Body,
    Left,
    Right,
    Top,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

/**
 * @brief What a drag of @p handle by @p delta makes of @p start: the body moves it, an
 *        edge or corner resizes it with the opposite side held where it stood.
 *
 * In canvas reference pixels, y down, rounded to whole ones as UI is authored. The
 * rect is placed at anchor + position - pivot * size, so an edge's move is split between
 * size and position by the pivot. An axis sized by size alone is never narrower than one
 * pixel. Free of ImGui so it is tested headlessly.
 *
 * @param start  The element as the drag began.
 * @param handle The part held.
 * @param delta  The pointer's travel since, in reference pixels.
 * @return The element as the drag leaves it.
 */
inline UIElement dragRect(UIElement start, RectHandle handle, glm::vec2 delta) {
    if (handle == RectHandle::None) return start;
    if (handle == RectHandle::Body) {
        start.position = glm::round(start.position + delta);
        return start;
    }
    const bool left   = handle == RectHandle::Left || handle == RectHandle::TopLeft
        || handle == RectHandle::BottomLeft;
    const bool right  = handle == RectHandle::Right || handle == RectHandle::TopRight
        || handle == RectHandle::BottomRight;
    const bool top    = handle == RectHandle::Top || handle == RectHandle::TopLeft
        || handle == RectHandle::TopRight;
    const bool bottom = handle == RectHandle::Bottom || handle == RectHandle::BottomLeft
        || handle == RectHandle::BottomRight;

    // Growth on each axis, signed so the held side's edge moves by delta, kept to a pixel wide
    // where size is the whole of it; a stretched axis (relativeSize) adds its parent's share,
    // which size alone cannot see, so its size may go negative, an inset.
    const auto grow = [](float size, float by, bool stretched) {
        return std::round(stretched ? size + by : std::max(size + by, 1.0f)) - size;
    };
    const bool  wide = start.relativeSize.x != 0.0f;
    const bool  tall = start.relativeSize.y != 0.0f;
    const float dx = right ? grow(start.size.x, delta.x, wide)
        : left ? -grow(start.size.x, -delta.x, wide) : 0.0f;
    const float dy = bottom ? grow(start.size.y, delta.y, tall)
        : top ? -grow(start.size.y, -delta.y, tall) : 0.0f;
    if (right)  start.position.x += dx * start.pivot.x;
    if (left)   start.position.x += dx * (1.0f - start.pivot.x);
    if (bottom) start.position.y += dy * start.pivot.y;
    if (top)    start.position.y += dy * (1.0f - start.pivot.y);
    start.size.x += right ? dx : -dx;
    start.size.y += bottom ? dy : -dy;
    return start;
}

} // namespace Vkm::Engine
