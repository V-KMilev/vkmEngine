#include "framework/editor_shortcuts.h"
#include "framework/editor_context.h"
#include "framework/editor_common.h"
#include "framework/scene_io_controller.h"
#include "input/editor_actions.h"

#include <imgui.h>

#include "core/system.h"
#include "system/camera/camera_controller.h"

namespace Engine {

void EditorShortcuts::process(EditorContext& ec, SceneIOController& sceneIO) {
    if (ImGui::GetIO().WantTextInput) return;

    FrameContext&     ctx    = ec.frame;
    EditorState&      state  = ec.state;
    CameraController* camera = ec.cameraController;
    const auto&       kb     = state.keybinds;

    if (isPressed(kb.toggleStats))     state.showStats     = !state.showStats;
    if (isPressed(kb.toggleHierarchy)) state.showHierarchy = !state.showHierarchy;
    if (isPressed(kb.toggleInspector)) state.showInspector = !state.showInspector;
    if (isPressed(kb.toggleBottom))    state.showBottom    = !state.showBottom;
    // NOTE: toggleEditor (F5) is intentionally not handled here; it is
    // owned by EditorSystem via raw GLFW so it works while the editor is
    // hidden (no ImGui frame) and cannot double-fire across both paths.
    if (isPressed(kb.openPreferences)) state.showPreferences = !state.showPreferences;

    if (isPressed(kb.saveSceneAs))     sceneIO.requestSaveAs();
    else if (isPressed(kb.saveScene))  sceneIO.save(ctx);
    if (isPressed(kb.loadScene))       sceneIO.requestLoad();

    if (isPressed(kb.deleteEntity) && state.selectedEntity && ctx.scene.isAlive(state.selectedEntity)) {
        EditorActions::deleteEntity(ctx.scene, state, state.selectedEntity);
    }
    if (isPressed(kb.deselect)) {
        state.selectedEntity = {};
    }
    if (isPressed(kb.duplicate) && state.selectedEntity && ctx.scene.isAlive(state.selectedEntity)) {
        EditorActions::duplicateEntity(ctx.scene, state, state.selectedEntity);
    }
    if (isPressed(kb.focusSelected) && state.selectedEntity && ctx.scene.isAlive(state.selectedEntity)) {
        EditorActions::focusOnSelected(ctx, state, camera);
    }

    // Gizmo mode shortcuts (only when camera NOT in fly mode)
    if (!camera->isLooking()) {
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
