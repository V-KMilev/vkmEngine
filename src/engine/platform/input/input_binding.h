#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "base/reflect/enum_names.h"
#include "vkm_platform/input/key.h"

namespace Vkm::Engine {

/**
 * @brief What physically produces an action's value: a key or a mouse button.
 *
 * A new device (gamepad) is a new alternative here, and InputMap::update reads it.
 */
using InputControl = std::variant<Platform::Key, Platform::MouseButton>;

/**
 * @brief One physical control contributing to an action.
 *
 * `scale` makes an action an axis: W at +1 and S at -1 make "Move/Forward" read -1..1.
 */
struct InputBinding {
    InputControl control = Platform::Key::Space;
    float        scale   = 1.0f;  ///< Contribution to axis(); sign gives direction.

    /// A keyboard key, adding @p scale to its action's axis().
    static InputBinding key(Platform::Key pressed, float scale = 1.0f) { return {pressed, scale}; }

    /// A mouse button, adding @p scale to its action's axis().
    static InputBinding mouse(Platform::MouseButton button, float scale = 1.0f) { return {button, scale}; }

    /// Whether this is @p key, at any scale.
    bool is(Platform::Key key) const { return control == InputControl(key); }

    /// Whether this is @p button, at any scale.
    bool is(Platform::MouseButton button) const { return control == InputControl(button); }
};

/**
 * @brief A game's action and the controls that feed it unless the player rebinds it: an entry of
 *        project.json's `actions`.
 */
struct InputAction {
    std::string               name;      ///< "Move/Forward": the identity a saved binding names.
    std::vector<InputBinding> bindings;
};

/**
 * @brief What a binding is called, for a controls screen and in project.json: "W", "Left Shift",
 *        "Left mouse".
 *
 * In English and by position, as the keys are: the key a US layout prints there.
 *
 * @param binding The key or button.
 * @return Its name.
 */
std::string bindingName(const InputBinding& binding);

/**
 * @brief The key or button bindingName calls @p name, at a scale of 1.
 *
 * @param name A name as bindingName writes it.
 * @return The binding, or nothing when no key or button is called that.
 */
std::optional<InputBinding> bindingNamed(std::string_view name);

} // namespace Vkm::Engine

// The names project.json and a player's bindings file hold.
VKM_ENUM_NAMES(
    ::Vkm::Platform::Key,
    "Space",
    "'",
    ",",
    "-",
    ".",
    "/",
    "0",
    "1",
    "2",
    "3",
    "4",
    "5",
    "6",
    "7",
    "8",
    "9",
    ";",
    "=",
    "A",
    "B",
    "C",
    "D",
    "E",
    "F",
    "G",
    "H",
    "I",
    "J",
    "K",
    "L",
    "M",
    "N",
    "O",
    "P",
    "Q",
    "R",
    "S",
    "T",
    "U",
    "V",
    "W",
    "X",
    "Y",
    "Z",
    "[",
    "\\",
    "]",
    "`",
    "World 1",
    "World 2",
    "Escape",
    "Enter",
    "Tab",
    "Backspace",
    "Insert",
    "Delete",
    "Right",
    "Left",
    "Down",
    "Up",
    "Page Up",
    "Page Down",
    "Home",
    "End",
    "Caps Lock",
    "Scroll Lock",
    "Num Lock",
    "Print Screen",
    "Pause",
    "F1",
    "F2",
    "F3",
    "F4",
    "F5",
    "F6",
    "F7",
    "F8",
    "F9",
    "F10",
    "F11",
    "F12",
    "F13",
    "F14",
    "F15",
    "F16",
    "F17",
    "F18",
    "F19",
    "F20",
    "F21",
    "F22",
    "F23",
    "F24",
    "F25",
    "Keypad 0",
    "Keypad 1",
    "Keypad 2",
    "Keypad 3",
    "Keypad 4",
    "Keypad 5",
    "Keypad 6",
    "Keypad 7",
    "Keypad 8",
    "Keypad 9",
    "Keypad .",
    "Keypad /",
    "Keypad *",
    "Keypad -",
    "Keypad +",
    "Keypad Enter",
    "Keypad =",
    "Left Shift",
    "Left Ctrl",
    "Left Alt",
    "Left Super",
    "Right Shift",
    "Right Ctrl",
    "Right Alt",
    "Right Super",
    "Menu"
)
VKM_ENUM_NAMES(
    ::Vkm::Platform::MouseButton,
    "Left mouse",
    "Right mouse",
    "Middle mouse",
    "Mouse 4",
    "Mouse 5",
    "Mouse 6",
    "Mouse 7",
    "Mouse 8"
)
