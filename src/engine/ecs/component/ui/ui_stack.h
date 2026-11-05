#pragma once

#include <cstdint>

#include "base/reflect/reflect.h"
#include "base/reflect/enum_names.h"

namespace Vkm::Engine {

/**
 * @brief Lays an element's children out one after another, in sibling order: a column of rows, or a
 *        row of icons.
 *
 * Each shown child takes a slot of its own size along the axis, the stack's whole extent across it,
 * and is placed in it by its anchor, pivot and position as in any parent: one made by UIElement::at
 * fills its slot, and its position nudges it. A hidden child takes no slot, so the rest close up.
 * The stack keeps its own size: what does not fit runs past its end, clipped only if it clips.
 */
struct UIStack {
    /**
     * @brief Which way the children follow one another.
     */
    enum class Axis : uint8_t {
        Vertical,    ///< Top to bottom: a column.
        Horizontal,  ///< Left to right: a row.
        Count        ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
    };

    Axis  axis    = Axis::Vertical;
    float spacing = 0.0f;  ///< Canvas reference pixels between one child and the next.
};

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::UIStack::Axis, "Vertical", "Horizontal")

VKM_REFLECT_BEGIN(::Vkm::Engine::UIStack)
    VKM_F(axis, "Vertical lays the children top to bottom, Horizontal left to right")
    VKM_F(spacing, 0.0f, 1000.0f, "Reference pixels between one child and the next")
VKM_REFLECT_END()
