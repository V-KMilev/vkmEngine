#pragma once

#include "core/math/curve.h"

namespace Vkm::Engine {

/**
 * @brief A property row editing a curve over a life: a plot of it, and its keys under it.
 *
 * An edit leaves the keys in time order, a key dragged past another changing place with it once
 * let go.
 *
 * @param label   The row's label and id scope.
 * @param curve   Curve whose keys the row edits.
 * @param lo      Lowest a key's value may be.
 * @param hi      Highest a key's value may be; the plot spans lo to hi, or to a loaded key above it.
 * @param tooltip Shown on hover, or null.
 * @return Whether it was edited this frame.
 */
bool propCurve(const char* label, Math::Curve& curve, float lo, float hi, const char* tooltip = nullptr);

/**
 * @brief A property row editing a gradient over a life: a strip of it, and its keys under it.
 *
 * An edit leaves the keys in time order, as propCurve's does.
 *
 * @param label    The row's label and id scope.
 * @param gradient Gradient whose keys the row edits.
 * @param tooltip  Shown on hover, or null.
 * @return Whether it was edited this frame.
 */
bool propGradient(const char* label, Math::Gradient& gradient, const char* tooltip = nullptr);

} // namespace Vkm::Engine
