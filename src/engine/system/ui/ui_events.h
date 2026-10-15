#pragma once

#include <string>

#include "ecs/entity.h"

namespace Vkm::Engine {

/**
 * @brief Fired when a UIButton is clicked (a press released over the same button).
 *
 * The UISystem enqueues this on the EventBus; it is delivered on the next
 * flush, so a Behavior handles it exactly like any other gameplay event:
 *
 *   subscribe([](const UIClickEvent& e) {
 *       if (e.eventId == "play") startGame();
 *   });
 */
struct UIClickEvent {
    EntityId    entity;   ///< The button entity that was clicked.
    std::string eventId;  ///< The clicked UIButton's `eventId`.
};

/**
 * @brief Fired when a UISlider is dragged to a new value, a UIToggle is flipped or a UIChoice is
 *        stepped.
 *
 * Enqueued by the UISystem like UIClickEvent, which a toggle's or a choice's click fires as well.
 */
struct UIValueEvent {
    EntityId    entity;   ///< The slider's, toggle's or choice's entity.
    std::string eventId;  ///< Its UIButton's `eventId`; for a choice, the clicked arrow's.
    float       value;    ///< The slider's value; a toggle's 1 on, 0 off; a choice's index.
};

} // namespace Vkm::Engine
