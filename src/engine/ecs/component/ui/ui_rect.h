#pragma once

#include <glm/glm.hpp>

namespace Vkm::Engine {

/**
 * @brief An axis-aligned screen-space rectangle (top-left origin, pixels).
 */
struct UIRect {
    glm::vec2 pos  = {0.0f, 0.0f};  ///< Top-left corner.
    glm::vec2 size = {0.0f, 0.0f};

    glm::vec2 max() const { return pos + size; }

    bool contains(const glm::vec2& point) const {
        return point.x >= pos.x && point.x < pos.x + size.x
            && point.y >= pos.y && point.y < pos.y + size.y;
    }

    /**
     * @brief The overlap of this rect and @p other, empty when they miss.
     *
     * Never a negative size, since clipping composes down the hierarchy by intersecting.
     *
     * @param other Rect to intersect with.
     * @return The overlap, or a zero-size rect at this rect's corner.
     */
    UIRect intersected(const UIRect& other) const {
        const glm::vec2 lo = glm::max(pos, other.pos);
        const glm::vec2 hi = glm::min(max(), other.max());
        return UIRect{lo, glm::max(hi - lo, glm::vec2(0.0f))};
    }
};

} // namespace Vkm::Engine
