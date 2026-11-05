#pragma once

#include <string>
#include <vector>

#include "base/reflect/reflect.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

/**
 * @brief One of a list of options, stepped through by two UIButtons: a setting's "< High >" row.
 *
 * A click on `back` or `next` steps `index` one way, stopping at either end, as a setting's levels
 * run from least to most, and the UISystem enqueues a UIValueEvent for the choice with the new
 * index, before the click's UIClickEvent; a click at the end it points to changes nothing.
 * Each frame the option at `index` is written into `text`'s UIText, so the text is best the
 * choice itself or its child; the buttons are best its children, their look the project's. A
 * button that steps several choices, or flips a toggle too, sends a UIValueEvent for each.
 */
struct UIChoice {
    std::vector<std::string> options;    ///< What `text` shows, in step order.
    int                      index = 0;  ///< Write it to set the choice; out of range shows nothing.

    EntityId back{};  ///< The UIButton stepping toward the first option.
    EntityId next{};  ///< The UIButton stepping toward the last.
    EntityId text{};  ///< Its UIText shows the option; none for no text.
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::UIChoice)
    VKM_F(options, "One a line, in step order")
    VKM_F(index, "Stepped by the arrows; write it to set it")
VKM_REFLECT_END()
