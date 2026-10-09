#pragma once

#include <glm/common.hpp>

#include "core/reflect.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

/**
 * @brief A value dragged along a UIButton's width: the button is the track.
 *
 * While a press that began on the button is held, the UISystem sets `value` from where
 * the pointer is across it and enqueues a UIValueEvent when that changes it. Every
 * frame it lays out the parts named: `fill` grows from the left to the value (the
 * slider owns its UIElement::relativeSize.x) and `knob` stands at it (UIElement::anchor.x).
 * Both are optional, and are best the track's children so they move with it. What
 * either looks like is the project's.
 */
struct UISlider {
    float value    = 0.0f;  ///< Write it to set the slider; a drag keeps it within min..max.
    float minValue = 0.0f;  ///< At the track's left end.
    float maxValue = 1.0f;  ///< At its right end.
    float step     = 0.0f;  ///< What a drag snaps to, from minValue; 0 is continuous.

    EntityId fill{};  ///< Grown to the value; none for no fill.
    EntityId knob{};  ///< Stood at the value; none for no knob.

    /**
     * @brief How far along the track the value is.
     *
     * @return 0 at minValue to 1 at maxValue; 0 for an empty range.
     */
    float fraction() const {
        const float range = maxValue - minValue;
        if (!(range > 0.0f)) return 0.0f;
        return glm::clamp((value - minValue) / range, 0.0f, 1.0f);
    }
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::UISlider)
    VKM_F(value)
    VKM_F(minValue)
    VKM_F(maxValue)
    VKM_F(step)
VKM_REFLECT_END()
