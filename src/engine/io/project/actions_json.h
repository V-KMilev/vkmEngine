#pragma once

#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "platform/input/input_binding.h"

namespace Vkm::Engine {

/**
 * @brief Actions as project.json's `actions` and a player's bindings file hold them: each name a key,
 *        each binding its name, or `{"bind": name, "scale": s}` for another scale.
 *
 * A binding that cannot be read is skipped with a warning, and an action left with none of the
 * ones it listed is skipped whole, so it keeps what it had.
 *
 * @param doc The object; anything else reads as none.
 * @return The actions, in name order.
 */
std::vector<InputAction> actionsFromJson(const nlohmann::json& doc);

/// @p actions as actionsFromJson reads them.
nlohmann::json actionsToJson(const std::vector<InputAction>& actions);

} // namespace Vkm::Engine
