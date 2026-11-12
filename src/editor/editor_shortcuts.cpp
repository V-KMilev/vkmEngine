#include "editor_shortcuts.h"

#include <algorithm>

#include <imgui.h>

#include "editor_context.h"
#include "ecs/scene.h"
#include "editor_state.h"
#include "input/editor_keybinds.h"
#include "session/scene_session.h"
#include "actions/edit_actions.h"
#include "actions/entity_clipboard.h"
#include "view/view_framing.h"
#include "io/project/roots.h"
#include "core/frame_context.h"
#include "view/camera_controller_system.h"
#include "overlays/terrain_tool.h"

namespace Vkm::Engine::EditorShortcuts {

namespace {

constexpr float TERRAIN_BRUSH_STEP = 1.25f;  ///< What one press of a brush-size key multiplies or divides the
                                             ///< radius by.

} // namespace

void process(EditorContext& ec) {
    SceneSession& session = ec.session;
    FrameContext& ctx = ec.frame;
    EditorState& state = ec.state;
    Workspace& workspace = state.workspace;
    CameraControllerSystem& camera = ec.cameraController;
    const auto& kb = state.prefs.keybinds;

    // No key repeat: a held toggle would flip back and forth, a save write every repeat.
    if (isPressed(kb.toggleHierarchy, false)) workspace.showHierarchy = !workspace.showHierarchy;
    if (isPressed(kb.toggleInspector, false)) workspace.showInspector = !workspace.showInspector;
    if (isPressed(kb.toggleAssets, false))    workspace.showAssets    = !workspace.showAssets;
    if (isPressed(kb.openPreferences, false)) state.showPreferences = !state.showPreferences;

    if (isPressed(kb.saveSceneAs, false))     session.requestSaveAs(state);
    else if (isPressed(kb.saveScene, false))  session.save(ctx, state);
    if (isPressed(kb.loadScene, false))       session.requestLoad();
    if (isPressed(kb.newScene, false)) state.sceneActions.request(SceneActions::Kind::New);
    if (isPressed(kb.buildScripts, false)) {
        state.requests.ask(VkmRun{{"build", Roots::projectRoot().string()}, {}});
    }

    if (isPressed(kb.toggleRenderSettings, false)) state.showRenderSettings = !state.showRenderSettings;
    if (isPressed(kb.toggleTerrain, false))        workspace.showTerrain        = !workspace.showTerrain;
    if (isPressed(kb.showConsole, false))          state.requests.ask(Ask::RevealConsole);

    // One or the other, should a rebind give both the same combo.
    if (isPressed(kb.redo))      EditorActions::redo(ctx.scene, state);
    else if (isPressed(kb.undo)) EditorActions::undo(ctx.scene, state);

    const bool holdsOne = state.selection.active() && ctx.scene.isAlive(state.selection.active());
    if (isPressed(kb.deleteEntity) && holdsOne) {
        EditorActions::deleteSelection(ctx.scene, state);
    }
    // With a terrain brush up, the first press puts it down and keeps the selection.
    if (isPressed(kb.deselect)) {
        if (terrainToolApplies(ec)) state.terrainTool.tool = TerrainTool::None;
        else                        state.selection.clear();
    }
    if (isPressed(kb.duplicate) && state.selection.active() && ctx.scene.isAlive(state.selection.active())) {
        EditorActions::duplicateSelection(ctx.scene, ctx.resources, state);
    }
    // The Console's line, when its list has the keyboard, is what a copy takes.
    const bool copiesEntities = !state.consoleHoldsKeys;
    if (copiesEntities && isPressed(kb.copyEntity, false)) {
        EditorActions::copySelection(ctx.scene, ctx.resources, state);
    }
    if (copiesEntities && isPressed(kb.cutEntity, false)) {
        EditorActions::cutSelection(ctx.scene, ctx.resources, state);
    }
    if (isPressed(kb.pasteEntity, false)) EditorActions::paste(ctx.scene, ctx.resources, state);
    if (isPressed(kb.selectAll, false))   EditorActions::selectAll(ctx.scene, state);
    // Over a preview, the keys that frame a view are the preview's (PreviewViewport).
    const bool framesScene = !state.previewHovered;
    const bool held        = state.selection.active() && ctx.scene.isAlive(state.selection.active());
    if (framesScene && isPressed(kb.focusSelected) && held) {
        ViewFraming::frameSelected(ctx, state.selection.entities(), camera);
    }
    if (framesScene && isPressed(kb.alignWithView, false)) {
        EditorActions::alignWithView(ctx.scene, ctx.resources, state);
    }
    if (isPressed(kb.dropToGround, false)) EditorActions::dropToGround(ctx.scene, ctx.resources, state);
    if (isPressed(kb.groupSelection, false)) EditorActions::groupSelection(ctx.scene, state);
    if (isPressed(kb.hideSelection, false)) {
        for (const EntityId id : state.selection.entities()) {
            if (!isIn(state.hiddenInView, id)) state.hiddenInView.push_back(id);
        }
    }
    if (isPressed(kb.showAll, false)) state.hiddenInView.clear();
    if (framesScene && isPressed(kb.frameAll)) {
        ViewFraming::frameAll(ctx, camera);
    }
    if (isPressed(kb.toggleOrthographic) && camera.isActive()) {
        camera.setOrthographic(!camera.isOrthographic());
    }

    // Not while the cursor is captured: flying reads the same letters as moves.
    if (ec.input.pointer != PointerOwner::Captured) {
        if (isPressed(kb.gizmoSelect))      workspace.tool = EditorTool::Select;
        if (isPressed(kb.gizmoTranslate))   workspace.tool = EditorTool::Translate;
        if (isPressed(kb.gizmoRotate))      workspace.tool = EditorTool::Rotate;
        if (isPressed(kb.gizmoScale))       workspace.tool = EditorTool::Scale;
        if (isPressed(kb.gizmoToggleSpace)) {
            const bool local = workspace.gizmoMode == GizmoMode::Local;
            workspace.gizmoMode = local ? GizmoMode::World : GizmoMode::Local;
        }
        // A quarter larger or smaller a press: from a path to a hill in ten.
        TerrainToolSettings& brush = state.terrainTool;
        if (terrainToolApplies(ec)) {
            if (isPressed(kb.terrainBrushSmaller)) brush.radius /= TERRAIN_BRUSH_STEP;
            if (isPressed(kb.terrainBrushLarger))  brush.radius *= TERRAIN_BRUSH_STEP;
            brush.radius = std::clamp(brush.radius, TERRAIN_BRUSH_MIN_RADIUS, TERRAIN_BRUSH_MAX_RADIUS);
        }
    }
}

} // namespace Vkm::Engine::EditorShortcuts
