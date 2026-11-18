#pragma once

#include "ecs/entity.h"

namespace Vkm::Engine {

/**
 * @brief Fired when a UIButton is clicked (a press released over the same button).
 *
 * The UISystem enqueues this on the EventBus; it is delivered on the next flush. A behavior hears
 * its own buttons through BehaviorBase::onClick.
 */
struct UIClickEvent {
    EntityId entity;  ///< The button entity that was clicked.
};

/**
 * @brief Fired when a UISlider is dragged to a new value, a UIToggle is flipped or a UIChoice is
 *        stepped; BehaviorBase::onChange hears it.
 *
 * Enqueued by the UISystem like UIClickEvent, which a toggle's or a choice's arrow's click fires
 * as well. Only the pointer's change sends one: a value the game writes shows without it.
 */
struct UIValueEvent {
    EntityId entity;  ///< The slider's, toggle's or choice's entity.
    float    value;   ///< The slider's value; a toggle's 1 on, 0 off; a choice's index.
};

} // namespace Vkm::Engine
