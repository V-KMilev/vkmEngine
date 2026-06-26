#pragma once

#include <string>
#include <vector>

#include "ecs/entity.h"
#include "framework/command_stack.h"
#include "input/editor_keybinds.h"
#include "gizmo/transform_gizmo.h"
#include "resource/asset/material_asset.h"

namespace Engine {

/**
 * @brief Shared editor state passed to all panels by reference.
 *
 * This is a plain data struct (no getters/setters) because it is internal
 * editor state shared among tightly-coupled panels. Panels read and write
 * fields directly. The compiler catches all renames immediately.
 *
 * Owned by EditorSystem, passed as EditorState& to each panel's draw().
 */
struct EditorState {
    // Selection
    EntityId selectedEntity{};
    bool     worldSelected = false;

    // Gizmo config
    GizmoOperation gizmoOperation = GizmoOperation::Translate;
    GizmoMode      gizmoMode      = GizmoMode::Local;

    // Snap settings
    bool  snapEnabled    = false;
    float snapTranslate  = 1.0f;
    float snapRotate     = 15.0f;    ///< Degrees
    float snapScale      = 0.1f;

    // Keybind configuration
    EditorKeybinds keybinds;

    // Panel visibility
    bool showHierarchy   = true;
    bool showInspector   = true;
    bool showBottom      = true;
    bool showPreferences = false;   ///< Preferences window (Ctrl+,)
    bool showMaterialEditor = false;            ///< Material Editor window
    MaterialHandle materialEditorTarget{};      ///< Which material it edits (else: selected entity's)
    bool showAssetBrowser   = false;            ///< Asset Browser window (material/mesh thumbnail grid)
    bool showRenderSettings = false;            ///< Render Settings window (pass toggles + per-effect tuning)
    bool showPhysics        = false;            ///< Physics window (per-scene gravity + solver settings)
    bool showColliders      = false;            ///< Draw physics collider wireframes in the viewport (View menu)
    bool showBounds         = false;            ///< Draw per-entity world AABBs in the viewport (View menu)
    int  colliderFitDetail  = 4;                ///< Voxel resolution for the Collider "Fit to Mesh" button

    // Layout dimensions (pixels)
    float leftPanelWidth    = 260.0f;
    float rightPanelWidth   = 340.0f;
    float bottomPanelHeight = 200.0f;

    // Flags
    bool viewportHovered = false;    ///< Whether mouse is over viewport
    bool hierarchyDirty  = true;     ///< Set by entity ops, consumed by HierarchyPanel
    bool editorVisible   = true;     ///< Toggle entire editor UI (F5)
    bool requestModelImport = false;  ///< Set by the Import Model menu item, consumed by the menu-bar dialog
    bool requestScriptReload = false; ///< Set by the Reload Scripts menu item, consumed by EditorSystem (hot-reload)

    // Scene I/O state
    bool sceneDirty = false;    ///< Unsaved edits since last save/load. Title shows '*'.
    bool confirmingQuit  = false;  ///< Save-on-quit modal is open this frame.
    bool closeAfterSave  = false;  ///< Window-close pending until the next clean save.
    std::vector<std::string> recentScenes;  ///< MRU list (absolute paths), most-recent first.
    static constexpr size_t MAX_RECENT_SCENES = 8;

    // Undo/redo history. Every editor mutation that goes through the
    // command path lands here; Ctrl+Z reverses, Ctrl+Shift+Z re-applies.
    // Cleared on scene load (entity IDs across scenes aren't comparable).
    CommandStack commands;

    // Floating-toast notification. Set by pushToast(); ticked down each
    // frame by EditorSystem; rendered as a corner overlay. Keeps save/load
    // failures (and other transient feedback) on-screen instead of
    // burying them in the console log.
    enum class ToastKind { Info, Warning, Error };
    std::string toastMessage;
    float       toastTimeRemaining = 0.0f;
    ToastKind   toastKind          = ToastKind::Info;

    /**
     * @brief Mark the scene as having unsaved changes. Call from every code path
     * that mutates the live Scene (entity ops, gizmo drags, inspector edits).
     * Cheap, idempotent.
     */
    void markSceneDirty() { sceneDirty = true; }

    /**
     * @brief Selection helpers - selectedEntity and worldSelected are mutually
     * exclusive, so always route selection through these (not raw assignment)
     * to keep that invariant without a draw-time fixup.
     */
    void selectEntity(EntityId id) { selectedEntity = id; worldSelected = false; }
    void selectWorld()             { selectedEntity = {}; worldSelected = true; }
    void deselect()                { selectedEntity = {}; worldSelected = false; }

    /**
     * @brief Show a transient toast at the corner of the editor. `seconds` <= 0
     * uses a kind-appropriate default. Replaces any prior toast.
     */
    void pushToast(ToastKind kind, std::string msg, float seconds = 0.0f) {
        if (seconds <= 0.0f) {
            seconds = (kind == ToastKind::Error) ? 6.0f
                    : (kind == ToastKind::Warning) ? 4.0f : 2.5f;
        }
        toastKind          = kind;
        toastMessage       = std::move(msg);
        toastTimeRemaining = seconds;
    }
};

} // namespace Engine
