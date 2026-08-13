#pragma once

#include <string>

#include "ecs/entity.h"

namespace Engine {

/**
 * @brief Fired when a UIButton is clicked (a press released over the same button).
 *
 * The UISystem enqueues this on the EventBus; it is delivered on the next
 * flush, so a Behavior handles it exactly like any other gameplay event:
 *
 *   subscribe<UIClickEvent>([](const UIClickEvent& e) {
 *       if (e.eventId == "play") startGame();
 *   });
 */
struct UIClickEvent {
    EntityId    entity;   ///< The button entity that was clicked.
    std::string eventId;  ///< The clicked UIButton's `eventId`.
};

} // namespace Engine
