#include "framework/editor_shortcuts.h"

#include <imgui.h>

#include "framework/editor_context.h"
#include "framework/editor_common.h"
#include "framework/scene_io_controller.h"
#include "framework/editor_actions.h"
#include "core/system.h"
#include "system/camera/camera_controller_system.h"

namespace Engine {

void EditorShortcuts::process(EditorContext& ec, SceneIOController& sceneIO) {
    if (ImGui::GetIO().WantTextInput) return;

    FrameContext& ctx = ec.frame;
    EditorState& state = ec.state;
    CameraControllerSystem& camera = ec.cameraController;
    const auto& kb = state.keybinds;

    if (isPressed(kb.toggleHierarchy)) state.showHierarchy = !state.showHierarchy;
    if (isPressed(kb.toggleInspector)) state.showInspector = !state.showInspector;
    if (isPressed(kb.toggleBottom))    state.showBottom    = !state.showBottom;
    // NOTE: toggleEditor is handled at EditorSystem::update directly so it
    // runs in both visible and hidden states (the ImGui frame exists in
    // both). Putting it here would never fire when the editor is hidden.
    if (isPressed(kb.openPreferences)) state.showPreferences = !state.showPreferences;

    if (isPressed(kb.saveSceneAs))     sceneIO.requestSaveAs();
    else if (isPressed(kb.saveScene))  sceneIO.save(ctx, state);
    if (isPressed(kb.loadScene))       sceneIO.requestLoad();
    if (isPressed(kb.newScene)) {
        if (state.sceneDirty) state.confirmAction = EditorState::PendingSceneAction::New;
        else                  sceneIO.newScene(ctx, state);
    }

    if (isPressed(kb.toggleRenderSettings)) state.showRenderSettings = !state.showRenderSettings;
    if (isPressed(kb.toggleMaterialEditor)) state.showMaterialEditor = !state.showMaterialEditor;
    if (isPressed(kb.toggleAssetBrowser))   state.showAssetBrowser   = !state.showAssetBrowser;

    // Undo / redo - checked in this order so Ctrl+Shift+Z (redo) wins
    // when both bindings would match.
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
        EditorActions::focusOnSelected(ctx, state, camera);
    }
    if (isPressed(kb.frameAll)) {
        EditorActions::frameAll(ctx, camera);
    }

    // Gizmo mode shortcuts (only when camera NOT in fly mode)
    if (!camera.isLooking()) {
        if (isPressed(kb.gizmoSelect))      state.gizmoOperation = GizmoOperation::Select;
        if (isPressed(kb.gizmoTranslate))   state.gizmoOperation = GizmoOperation::Translate;
        if (isPressed(kb.gizmoRotate))      state.gizmoOperation = GizmoOperation::Rotate;
        if (isPressed(kb.gizmoScale))       state.gizmoOperation = GizmoOperation::Scale;
        if (isPressed(kb.gizmoToggleSpace)) {
            state.gizmoMode = (state.gizmoMode == GizmoMode::Local) ? GizmoMode::World : GizmoMode::Local;
        }
    }
}

} // namespace Engine
