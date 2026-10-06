#pragma once

#include <algorithm>
#include <cmath>

namespace Vkm::Engine {

/**
 * @brief The steps a gizmo drag moves in while snapping is on.
 *
 * Steps count from where the drag began, along the handle held, so a value not on a
 * multiple is not pulled onto one. Zero leaves that measure free. Free of ImGui so it
 * is tested headlessly.
 */
struct GizmoSnap {
    float distance = 0.0f;   ///< World units.
    float angle    = 0.0f;   ///< Radians.
    float scale    = 0.0f;   ///< Scale units.
};

/**
 * @brief How far a drag has travelled, in whole steps.
 *
 * @param travelled Signed distance or angle from where the drag began.
 * @param step Zero or less leaves @p travelled as it is.
 * @return @p travelled rounded to the nearest multiple of @p step.
 */
inline float snapTravel(float travelled, float step) {
    if (step <= 0.0f) return travelled;
    return std::round(travelled / step) * step;
}

/**
 * @brief A scale dragged from @p start to @p dragged, moved in whole steps.
 *
 * Steps count from the start, so an import at 0.01 is not rounded to zero. A step down
 * never lands below one step unless the scale began there: zero scale makes every
 * child's matrix invert to NaN.
 *
 * @param start Magnitude when the drag began; positive.
 * @param dragged Magnitude reached; positive.
 * @param step Zero or less leaves @p dragged as it is.
 * @return The snapped magnitude, always positive.
 */
inline float snapScale(float start, float dragged, float step) {
    if (step <= 0.0f) return dragged;
    const float snapped = start + snapTravel(dragged - start, step);
    return snapped < step && snapped < start ? std::min(start, step) : snapped;
}

} // namespace Vkm::Engine
