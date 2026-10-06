#pragma once

#include <imgui.h>

#include "input/input_ownership.h"

namespace Vkm::Engine {

struct FrameContext;
struct EditorState;
class EngineErrorLog;
class AudioSystem;
class CameraControllerSystem;
class MaterialPreviewSession;
class RenderSystem;

/**
 * @brief Everything a panel needs for one frame, in one place.
 *
 * Collaborators are non-owning references. The event bus rides frame.events.
 */
struct EditorContext {
    FrameContext& frame;
    EditorState&  state;

    CameraControllerSystem& cameraController;
    RenderSystem&           renderSystem;
    MaterialPreviewSession& materialPreviews;

    /**
     * @brief The mixer, for auditioning.
     *
     * An audition has no entity, so it reaches the device, not the components.
     */
    AudioSystem&            audioSystem;

    /// Editor-owned recoverable-error log the engine reports into.
    EngineErrorLog&         errorLog;

    /**
     * @brief The Viewport window's content rect in screen space.
     *
     * Set each frame just before the overlays are drawn.
     */
    ImVec2 viewportPos{};
    ImVec2 viewportSize{};  ///< See viewportPos.

    /**
     * @brief Who has the pointer and the keyboard.
     *
     * Decided before anything is drawn.
     */
    InputOwnership input{};
};

} // namespace Vkm::Engine
