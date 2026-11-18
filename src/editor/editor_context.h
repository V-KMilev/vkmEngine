#pragma once

#include <imgui.h>

#include "input/input_ownership.h"

namespace Vkm::Engine {

struct FrameContext;
struct EditorState;
class EngineErrorLog;
class AudioSystem;
class LoadingSystem;
class CameraControllerSystem;
class PreviewCache;
class EditorRenderHooks;
class RenderSystem;
class PlaySession;
class SceneSession;

/**
 * @brief Everything a panel needs for one frame; non-owning references, the event bus in frame.events.
 */
struct EditorContext {
    FrameContext& frame;
    EditorState&  state;

    CameraControllerSystem& cameraController;
    RenderSystem&           renderSystem;
    EditorRenderHooks&      hooks;  ///< The backend's editor half, which the editor requires.
    PreviewCache&           previews;

    AudioSystem&            audioSystem;  ///< The mixer, for auditioning: an audition has no entity,
                                          ///< so it reaches the device, not the components.

    LoadingSystem&          loadingSystem;  ///< Shows the project's loading screen: given it as the
                                            ///< project opens and as it is saved.

    EngineErrorLog&         errorLog;  ///< Editor-owned recoverable-error log the engine reports into.

    SceneSession& session;  ///< The scene's file, and Stop putting Play's world back.
    PlaySession&  play;     ///< Play until Stop: the world as Play found it, and the ejected view.

    ImVec2 viewportPos{};   ///< The Viewport's content rect in screen space, set before the overlays draw.
    ImVec2 viewportSize{};  ///< See viewportPos.

    InputOwnership input{};  ///< Who has the pointer and the keyboard, decided before anything is drawn.
};

} // namespace Vkm::Engine
