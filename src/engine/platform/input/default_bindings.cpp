#include "platform/input/default_bindings.h"

#include "platform/input/input_map.h"
#include "platform/window/glfw_include.h"

namespace Engine {

namespace {

/**
 * @brief A keyboard binding contributing @p scale to its action's axis.
 */
InputBinding key(int code, float scale = 1.0f) {
    return InputBinding{InputSource::Key, code, scale};
}

} // namespace

void installDefaultBindings(InputMap& map) {
    // Paired keys on one axis, so opposing presses cancel in the map rather
    // than in every caller.
    map.define(InputActions::MOVE_FORWARD, { key(GLFW_KEY_W,  1.0f), key(GLFW_KEY_S, -1.0f) });
    map.define(InputActions::MOVE_RIGHT,   { key(GLFW_KEY_D,  1.0f), key(GLFW_KEY_A, -1.0f) });
    map.define(InputActions::MOVE_UP,      { key(GLFW_KEY_Q,  1.0f), key(GLFW_KEY_E, -1.0f) });
    map.define(InputActions::BOOST,        { key(GLFW_KEY_LEFT_SHIFT) });
}

} // namespace Engine
