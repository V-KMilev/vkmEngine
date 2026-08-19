#pragma once

#include <imgui.h>

namespace Vkm::Engine {

struct FrameContext;
struct EditorState;
class EngineErrorLog;
class CameraControllerSystem;
class MaterialPreviewSession;
class RenderSystem;
class VisibilitySystem;

/**
 * @brief Everything a panel needs for one frame, in one place.
 *
 * Bundles the engine's per-frame FrameContext, the shared EditorState, the
 * editor-relevant engine systems, and the current viewport child rect. It
 * is passed by reference to every panel's draw(), so a panel never carries
 * injected system pointers of its own and every draw() has one signature.
 *
 * Collaborators are non-owning references: the editor is always constructed
 * with live RenderSystem / VisibilitySystem / CameraControllerSystem
 * instances (the editor app registers them before the EditorSystem), so panels
 * don't guard against nullptr. The event bus rides frame.events.
 */
struct EditorContext {
    FrameContext& frame;
    EditorState&  state;

    CameraControllerSystem& cameraController;
    RenderSystem&           renderSystem;
    VisibilitySystem&       visibilitySystem;
    MaterialPreviewSession& materialPreviews;

    // The editor-owned recoverable-error log (engine reports into it via the
    // reportError() sink). Read by the Bottom panel's Errors tab.
    EngineErrorLog&         errorLog;

    // Viewport child rect in screen space. Set by EditorSystem each frame
    // just before the in-viewport overlays are drawn.
    ImVec2 viewportPos{};
    ImVec2 viewportSize{};
};

} // namespace Vkm::Engine
