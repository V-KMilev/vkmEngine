#pragma once

namespace Vkm::Engine {

struct EditorContext;
class CameraControllerSystem;

/**
 * @brief Say why the viewport is empty when a session's game has no active Camera.
 *
 * @param ec Supplies the viewport rect.
 */
void drawNoCameraNotice(EditorContext& ec);

/**
 * @brief Say what the world is waiting on while it loads, with a bar of how far it is.
 *
 * Centered while programs compile and nothing shows, at the foot of the view while baking.
 *
 * @param ec Supplies the viewport rect and the frame's loading screen.
 */
void drawLoadingNotice(EditorContext& ec);

/**
 * @brief Show the fly speed for a moment after the wheel changes it, fading out.
 *
 * @param ec     Supplies the viewport rect.
 * @param camera Whose speed, and for how long it is still shown.
 */
void drawFlySpeed(EditorContext& ec, const CameraControllerSystem& camera);

} // namespace Vkm::Engine
