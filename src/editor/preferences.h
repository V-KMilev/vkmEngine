#pragma once

#include "base/reflect/reflect.h"
#include "input/editor_keybinds.h"
#include "vkm_platform/window/window_manager.h"
#include "view/camera_controller_system.h"

namespace Vkm::Engine {

/**
 * @brief What follows the person rather than the project, kept in the user file (EditorSettings::saveUser).
 *
 * EditorSystem applies them to the camera controller and the window.
 */
struct Preferences {
    EditorKeybinds keybinds;

    bool  snapEnabled   = false;
    float snapTranslate = 1.0f;
    float snapRotate    = 15.0f;  ///< Degrees.
    float snapScale     = 0.1f;

    CameraControllerSystem::Settings camera;

    float                uiScale    = 1.0f;                            ///< How much bigger the UI is than the
                                                                       ///< display asks for: a multiplier on
                                                                       ///< the window's content scale.
    bool                 vsync      = true;
    int                  fpsCap     = 0;                               ///< Frames per second the window is
                                                                       ///< held to; 0 is unlimited.
    Platform::WindowMode windowMode = Platform::WindowMode::Windowed;
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::Preferences)
    VKM_F(snapEnabled, "Snap every drag; Ctrl held snaps while it is off")
    VKM_F(snapTranslate, 0.01f, 100.0f, "The step a move snaps to, in meters")
    VKM_F(snapRotate, 1.0f, 180.0f, "The step a turn snaps to, in degrees")
    VKM_F(snapScale, 0.01f, 10.0f, "The step a scale snaps to")
    VKM_F(camera)
    VKM_F(uiScale, 0.5f, 4.0f, "How much bigger the editor is than the display asks for")
    VKM_F(vsync, "Sync to the display's refresh")
    VKM_F(fpsCap, 0.0f, 1000.0f, "Frames a second the window is held to; 0 is unlimited")
    VKM_F(windowMode)
VKM_REFLECT_END()
