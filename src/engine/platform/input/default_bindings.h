#pragma once

namespace Vkm::Engine {

class InputMap;

/**
 * @brief Action names the engine itself reads.
 *
 * String literals rather than an enum so a saved binding file and a controls
 * screen can refer to actions the engine has never heard of - a game defines
 * its own alongside these, and nothing in the engine needs to know about them.
 * Constants (rather than bare literals at the call site) only so a typo in the
 * engine's own actions is a link error instead of an action that silently never
 * fires.
 */
namespace InputActions {
    inline constexpr const char* MOVE_FORWARD = "Camera/Forward";  ///< Axis: +forward, -back.
    inline constexpr const char* MOVE_RIGHT   = "Camera/Right";    ///< Axis: +right, -left.
    inline constexpr const char* MOVE_UP      = "Camera/Up";       ///< Axis: +up, -down.
    inline constexpr const char* BOOST        = "Camera/Boost";    ///< Held: move faster.

    /**
     * @brief Press, hold and release of the game UI's primary pointer button.
     *
     * A named action rather than a raw button because it is rebindable like
     * anything else - a game on a gamepad presses its menus with a face button
     * - and because the map already samples once a frame and keeps the previous
     * value, which is the edge detection UISystem would otherwise carry itself
     * and get subtly wrong the moment anything else read the same button.
     */
    inline constexpr const char* UI_CLICK     = "UI/Click";        ///< Press/release: activate.
} // namespace InputActions

/**
 * @brief Install the bindings every host's own systems read.
 *
 * Currently one: the game UI's primary button. Games define their own on top; a
 * project that loads a saved binding file calls this first so an action missing
 * from the file still has a sensible default rather than being dead.
 *
 * @param map The map to populate.
 */
void installDefaultBindings(InputMap& map);

/**
 * @brief Install the fly camera's bindings.
 *
 * Separate because the camera controller is: it is an authoring tool, registered
 * only by `vkm_editor`, so a runtime that installed its bindings would carry
 * four actions nothing reads. Here rather than in the editor because the action
 * names belong beside the system that reads them.
 *
 * @param map The map to populate.
 */
void installEditorBindings(InputMap& map);

} // namespace Vkm::Engine
