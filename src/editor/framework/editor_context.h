#pragma once

#include <imgui.h>

namespace Engine {

struct FrameContext;
struct EditorState;
class Engine;
class EngineErrorLog;
class CameraControllerSystem;
class MaterialPreviewSession;
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
 *
 * Collaborators are non-owning references: the editor is always constructed
 * with live RenderSystem / VisibilitySystem / CameraControllerSystem / EventSystem
 * instances (the editor app registers them before the EditorSystem), so panels
 * don't guard against nullptr.
 */
struct EditorContext {
    FrameContext& frame;
    EditorState&  state;

    // The owning engine, for editor-only controls that reach past one frame's
    // FrameContext - chiefly the simulation HUD (play/pause/step). Non-owning;
    // the engine outlives the editor.
    Engine& engine;

    CameraControllerSystem&       cameraController;
    RenderSystem&           renderSystem;
    VisibilitySystem&       visibilitySystem;
    EventSystem&            events;
    MaterialPreviewSession& materialPreviews;

    // The editor-owned recoverable-error log (engine reports into it via the
    // reportError() sink). Read by the Bottom panel's Errors tab.
    EngineErrorLog&         errorLog;

    // Viewport child rect in screen space. Set by EditorSystem each frame
    // just before the in-viewport overlays are drawn.
    ImVec2 viewportPos{};
    ImVec2 viewportSize{};
};

} // namespace Engine
