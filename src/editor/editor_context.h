#pragma once

#include <imgui.h>

namespace Engine {

struct FrameContext;
struct EditorState;
class CameraController;
class RenderSystem;
class VisibilitySystem;
class EventSystem;

/**
 * @brief Everything a panel needs for one frame, in one place.
 *
 * Bundles the engine's per-frame FrameContext, the shared EditorState, the
 * editor-relevant engine systems, and the current viewport child rect. It
 * is passed by reference to every panel's draw(), so a panel never carries
 * injected system pointers of its own and every draw() has one signature.
 */
struct EditorContext {
    FrameContext& frame;
    EditorState&  state;

    CameraController* cameraController = nullptr;
    RenderSystem*     renderSystem     = nullptr;
    VisibilitySystem* visibilitySystem = nullptr;
    EventSystem*      events           = nullptr;

    // Viewport child rect in screen space. Set by EditorSystem each frame
    // just before the in-viewport overlays are drawn.
    ImVec2 viewportPos{};
    ImVec2 viewportSize{};
};

} // namespace Engine
