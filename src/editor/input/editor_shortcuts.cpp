#include "input/editor_shortcuts.h"

#include <algorithm>

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
#include "overlays/terrain_tool.h"

namespace Vkm::Engine::EditorShortcuts {

namespace {

/// What one press of a brush-size key multiplies or divides the radius by.
constexpr float TERRAIN_BRUSH_STEP = 1.25f;

} // namespace

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
    if (isPressed(kb.toggleTerrain, false))        state.showTerrain        = !state.showTerrain;
    if (isPressed(kb.showConsole, false))          state.revealConsole      = true;

    // One or the other, should a rebind give both the same combo.
    if (isPressed(kb.redo))      EditorActions::redo(ctx.scene, state);
    else if (isPressed(kb.undo)) EditorActions::undo(ctx.scene, state);

    if (isPressed(kb.deleteEntity) && state.selectedEntity && ctx.scene.isAlive(state.selectedEntity)) {
        EditorActions::deleteSelection(ctx.scene, state);
    }
    // With a terrain brush up, the first press puts it down and keeps the selection.
    if (isPressed(kb.deselect)) {
        if (terrainToolApplies(ec)) state.terrainTool.tool = TerrainTool::None;
        else                        state.deselect();
    }
    if (isPressed(kb.duplicate) && state.selectedEntity && ctx.scene.isAlive(state.selectedEntity)) {
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
    const bool held = state.selectedEntity && ctx.scene.isAlive(state.selectedEntity);
    if (framesScene && isPressed(kb.focusSelected) && held) {
        ViewFraming::frameSelected(ctx, state.selection, camera);
    }
    if (framesScene && isPressed(kb.alignWithView, false)) {
        EditorActions::alignWithView(ctx.scene, ctx.resources, state);
    }
    if (isPressed(kb.dropToGround, false)) EditorActions::dropToGround(ctx.scene, ctx.resources, state);
    if (framesScene && isPressed(kb.frameAll)) {
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
