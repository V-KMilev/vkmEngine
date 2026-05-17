#pragma once

#include "ecs/entity.h"
#include "input/editor_keybinds.h"
#include "gizmo/transform_gizmo.h"
#include "resource/material_asset.h"  // MaterialHandle (Material Editor target)

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
    bool showStats       = true;
    bool showHierarchy   = true;
    bool showInspector   = true;
    bool showBottom      = true;
    bool showPreferences = false;   ///< Preferences window (Ctrl+,)
    bool showMaterialEditor = false;            ///< Material Editor window
    MaterialHandle materialEditorTarget{};      ///< Which material it edits (else: selected entity's)
    bool showAssetBrowser   = false;            ///< Asset Browser window (material/mesh thumbnail grid)

    // Layout dimensions (pixels)
    float leftPanelWidth    = 260.0f;
    float rightPanelWidth   = 340.0f;
    float bottomPanelHeight = 200.0f;

    // Flags
    bool wireframe      = false;     ///< Wireframe rendering mode
    bool viewportHovered = false;    ///< Whether mouse is over viewport
    bool hierarchyDirty  = true;     ///< Set by entity ops, consumed by HierarchyPanel
    bool editorVisible   = true;     ///< Toggle entire editor UI (F5)
    bool requestModelImport = false;  ///< Set by the Import Model menu item, consumed by the menu-bar dialog
};

} // namespace Engine
