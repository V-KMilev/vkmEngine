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
} // namespace InputActions

/**
 * @brief Install the engine's default bindings into @p map.
 *
 * Only the actions the engine's own systems read - currently the editor fly
 * camera. Games define their own on top; a project that loads a saved binding
 * file calls this first so an action missing from the file still has a sensible
 * default rather than being dead.
 *
 * @param map The map to populate.
 */
void installDefaultBindings(InputMap& map);

} // namespace Vkm::Engine
