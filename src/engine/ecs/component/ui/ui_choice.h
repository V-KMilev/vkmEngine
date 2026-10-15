#pragma once

#include <string>
#include <vector>

#include "core/reflect.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

/**
 * @brief One of a list of options, stepped through by two UIButtons: a setting's "< High >" row.
 *
 * A click on `back` or `next` steps `index` one way, wrapping at either end, and the UISystem
 * enqueues a UIValueEvent for the choice with the new index under the clicked button's eventId,
 * before the click's UIClickEvent. Each frame the option at `index` is written into `text`'s
 * UIText, so the text is best the choice itself or its child; the buttons are best its children,
 * their look the project's. A button that steps several choices, or flips a toggle too, sends a
 * UIValueEvent for each.
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
    VKM_F(options)
    VKM_F(index)
VKM_REFLECT_END()
