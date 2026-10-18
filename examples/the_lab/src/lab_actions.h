#pragma once

#include <string>
#include <string_view>

namespace Vkm::Engine {
    class InputMap;
}

namespace Lab {

// The lab's input actions, by name. Each is defined, on every end, in the onStart of the behavior
// that reads it: an action's slot is part of what a join checks.
inline constexpr const char* ACTION_FORWARD = "Move/Forward";
inline constexpr const char* ACTION_BACK    = "Move/Back";
inline constexpr const char* ACTION_LEFT    = "Move/Left";
inline constexpr const char* ACTION_RIGHT   = "Move/Right";
inline constexpr const char* ACTION_WALK    = "Walk";
inline constexpr const char* ACTION_JUMP    = "Jump";
inline constexpr const char* ACTION_USE     = "Use";
inline constexpr const char* ACTION_FIRE    = "Fire";
inline constexpr const char* ACTION_THROW   = "Throw";
inline constexpr const char* ACTION_LOOK    = "Look";
inline constexpr const char* ACTION_MAP     = "Map";
inline constexpr const char* ACTION_MENU    = "Menu";

/**
 * @brief What to press for @p action, for a hint on screen: the name of its first binding.
 *
 * @param input  The input map the action is defined in.
 * @param action Action name.
 * @return The binding's name (bindingName); empty when the action has none.
 */
std::string keyFor(const Vkm::Engine::InputMap& input, std::string_view action);

} // namespace Lab
