#include "input/editor_shortcuts.h"

#include <imgui.h>

#include "editor_context.h"
#include "ecs/scene.h"
#include "editor_state.h"
#include "input/editor_keybinds.h"
#include "session/scene_io_controller.h"
#include "editor_actions.h"
#include "input/view_framing.h"
#include "core/system.h"
#include "input/camera_controller_system.h"

namespace Vkm::Engine::EditorShortcuts {

void process(EditorContext& ec, SceneIOController& sceneIO) {
    FrameContext& ctx = ec.frame;
    EditorState& state = ec.state;
    CameraControllerSystem& camera = ec.cameraController;
    const auto& kb = state.prefs.keybinds;

    // No key repeat: a held toggle would flip back and forth, a save write every repeat.
    if (isPressed(kb.toggleHierarchy, false)) state.showHierarchy = !state.showHierarchy;
    if (isPressed(kb.toggleInspector, false)) state.showInspector = !state.showInspector;
    if (isPressed(kb.toggleAssets, false))    state.showAssets    = !state.showAssets;
    if (isPressed(kb.openPreferences, false)) state.showPreferences = !state.showPreferences;

    if (isPressed(kb.saveSceneAs, false))     sceneIO.requestSaveAs();
    else if (isPressed(kb.saveScene, false))  sceneIO.save(ctx, state);
    if (isPressed(kb.loadScene, false))       sceneIO.requestLoad();
    if (isPressed(kb.newScene, false)) state.requestSceneAction(EditorState::SceneAction::New);

    if (isPressed(kb.toggleRenderSettings, false)) state.showRenderSettings = !state.showRenderSettings;

    // One or the other, should a rebind give both the same combo.
    if (isPressed(kb.redo))      EditorActions::redo(ctx.scene, state);
    else if (isPressed(kb.undo)) EditorActions::undo(ctx.scene, state);

    if (isPressed(kb.deleteEntity) && state.selectedEntity && ctx.scene.isAlive(state.selectedEntity)) {
        EditorActions::deleteSelection(ctx.scene, state);
    }
    if (isPressed(kb.deselect)) {
        state.deselect();
    }
    if (isPressed(kb.duplicate) && state.selectedEntity && ctx.scene.isAlive(state.selectedEntity)) {
        EditorActions::duplicateSelection(ctx.scene, ctx.resources, state);
    }
    if (isPressed(kb.focusSelected) && state.selectedEntity && ctx.scene.isAlive(state.selectedEntity)) {
        ViewFraming::frameSelected(ctx, state.selectedEntity, camera);
    }
    if (isPressed(kb.frameAll)) {
        ViewFraming::frameAll(ctx, camera);
    }
    if (isPressed(kb.toggleOrthographic) && camera.isActive()) {
        camera.setOrthographic(!camera.isOrthographic());
    }

    // Not while the cursor is captured: flying reads the same letters as moves.
    if (ec.input.pointer != PointerOwner::Captured) {
        if (isPressed(kb.gizmoSelect))      state.tool = EditorTool::Select;
        if (isPressed(kb.gizmoTranslate))   state.tool = EditorTool::Translate;
        if (isPressed(kb.gizmoRotate))      state.tool = EditorTool::Rotate;
        if (isPressed(kb.gizmoScale))       state.tool = EditorTool::Scale;
        if (isPressed(kb.gizmoToggleSpace)) {
            state.gizmoMode = (state.gizmoMode == GizmoMode::Local) ? GizmoMode::World : GizmoMode::Local;
        }
    }
}

} // namespace Vkm::Engine::EditorShortcuts
