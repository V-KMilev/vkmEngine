# Editor — Independent Audit (post-refactor)

> Audit performed without consulting `docs/editor_refactor_audit.md` or memory.
> Pure file-by-file walkthrough of `src/editor/`.

## 1. Current State

### Layout (54 files, ~8.1k LoC)

```
src/editor/
  editor_system.{h,cpp}              ~ 280  loc   shell / orchestrator
  framework/                         9 files     long-lived editor scaffolding
    editor_context.h                  one-frame DI bundle
    editor_state.h                    shared mutable struct
    editor_common.h                   convenience prelude for panels
    editor_settings.{h,cpp}           JSON persistence (panel sizes, keybinds, MRU)
    editor_menu_bar.{h,cpp}           top-bar commands + ModelImport modal owner
    editor_status_bar.{h,cpp}         bottom strip (selection breadcrumb + build)
    editor_shortcuts.{h,cpp}          keybind → command dispatcher
    editor_panel_resize.{h,cpp}       borderless edge drag
    scene_io_controller.{h,cpp}       Save / Save-As / Load + dialogs
    asset_picker.{h,cpp}              cached modal filesystem picker
    screenshot.{h,cpp}                glReadPixels → PNG
  panels/                            docked EditorPanels
    hierarchy_panel.{h,cpp}           tree + filter + drag-reparent
    inspector_panel.{h,cpp}           per-component cards
    bottom_panel.{h,cpp}              Animation timeline + Statistics tabs
    environment_inspector.{h,cpp}     EnvironmentConfig editor (largest panel, 537 loc)
    preferences_panel.{h,cpp}         floating prefs window
    material_editor.{h,cpp}           floating PBR editor + live preview
    asset_browser.{h,cpp}             floating thumbnail grid
  overlays/                          in-viewport
    viewport_overlay.{h,cpp}          stats + navigation gizmo
    gizmo_overlay.{h,cpp}             transform gizmo + light/camera gizmos + picking
    viewport_toolbar.{h,cpp}          bottom-left tool strip
    playback_bar.{h,cpp}              top-centre play/stop
  ui/                                drawing primitives
    editor_theme.{h,cpp}              ImGui style + colors
    editor_style.h                    palette + metric constants
    editor_widgets.{h,cpp}            cards, vec3 control, EulerCache, etc.
    editor_icons.{h,cpp}              vector glyph set
  input/
    editor_keybinds.{h,cpp}           KeyBind struct + label helper
    editor_actions.{h,cpp}            createEntity / dup / delete / frame / import
  gizmo/                             4-file translation-unit split
    transform_gizmo.{h,cpp}           shell + math + state machine
    transform_gizmo_hit.cpp           hit-testing per operation
    transform_gizmo_drag.cpp          drag handlers
    transform_gizmo_draw.cpp          ImDrawList rendering
```

### What the refactor achieved (genuinely good)

1. **God-file decomposition is real.** `EditorSystem` is now a thin shell (~280 loc)
   that wires collaborators and delegates. Menu/status/shortcut/resize each have
   their own header + .cpp. `EditorActions` is a free-function namespace shared
   by 3 callers (menu, hierarchy, toolbar).
2. **`EditorContext` is a clean per-frame DI bundle.** Every panel has one
   signature: `void draw(EditorContext&)`. No more `Engine::get()` smell. The
   four engine deps (RenderSystem, VisibilitySystem, EventSystem,
   CameraController) are non-owning references, set at boot.
3. **Editor-rooted includes work.** `#include "framework/foo.h"`, never
   `"../foo.h"`. Honored consistently.
4. **Persistence is non-fatal.** `EditorSettings::load` gracefully falls back
   to defaults; ImGui's own `imgui.ini` is co-located alongside
   `editor_settings.json`.
5. **`AssetPicker` is a real, reusable abstraction.** Every dialog (Save-As,
   Load, IBL HDR browse, LUT browse, PBR folder, texture slot) goes through
   the same modal, with on-open cache instead of per-frame `directory_iterator`
   walks.
6. **`EulerCache<Key>` is a quietly excellent idea** — it solves a real
   gimbal-lock UX bug (quaternion → Euler → quaternion round-trip) in one
   small typed helper and is reused by both Inspector (keyed by entity) and
   Animation editor (keyed by keyframe index).
7. **Selection-aware affordances ripple correctly.** Toolbar duplicate/focus/
   delete buttons disable when nothing is selected; same logic in Hierarchy
   context menu; menu items use `MenuItem(..., enabled)`.
8. **Theme + style are split** (`editor_theme.cpp` configures ImGui;
   `editor_style.h` exposes constants). Component-card accent rails come from
   a single helper; the IBL/Post sections all read the same way.
9. **F5 toggle uses raw GLFW intentionally** with a documented reason (no
   ImGui frame while hidden); the trade-off is recorded inline.
10. **Status bar shows a parent breadcrumb** ("Root > Group > Leaf"), not
    just a leaf name — small detail, big UX win in deep hierarchies.
11. **`SceneIOController` does the post-load housekeeping itself** (camera
    rebind, TAA history invalidate) rather than self-subscribing — cleaner
    lifecycle, no `Engine::get()` reach.
12. **`materialPreviewTexture` is properly budgeted by RenderSystem with a
    key + version**, so the Asset Browser doesn't stall on first paint of a
    big grid.

This is a strong foundation. The structural work is done. What follows is the
list of issues left over and the higher-order gaps you'd want to close to
become "best-of" rather than "modern enough".

---

## 2. Correctness / Bugs

| # | Where | Issue |
|---|---|---|
| C1 | [gizmo_overlay.cpp:548-549](src/editor/overlays/gizmo_overlay.cpp#L548-L549) | `handleViewportPick` calls `state.markSceneDirty()` and sets `hierarchyDirty` on a **selection**. Selection is not part of the saved scene; the title gets a `*` and the user is prompted to save just for clicking. |
| C2 | [transform_gizmo_drag.cpp:78-94](src/editor/gizmo/transform_gizmo_drag.cpp#L78-L94) | `handleScaleDrag` calls `glm::decompose` on the start matrix **every drag frame**. For Transforms with non-uniform scale this is exactly the quaternion-decomposition hazard the rotation path goes out of its way to avoid; a 180° flip mid-drag is possible. Cache `startPos/Rot/Scale` once at drag-start. |
| C3 | [transform_gizmo.cpp:286-287](src/editor/gizmo/transform_gizmo.cpp#L286-L287) | `getDragRotation()` returns whatever was last set by `handleRotationDrag` — only cleared on rotation **release**. If a different operation drag is active, it returns a stale quaternion. Caller (`GizmoOverlay`) reads it inside an `if (operation == Rotate)`, so safe today, but brittle. Reset at start of every `manipulate`. |
| C4 | [asset_picker.cpp:54-57](src/editor/framework/asset_picker.cpp#L54-L57) | `consider()` pushes the entry **before** checking `maxResults`, so the cap is always exceeded by 1. The outer loop's `> options.maxResults` check is also strict, so the second over-cap entry is required to stop. Off-by-one. |
| C5 | [screenshot.cpp:46-47](src/editor/framework/screenshot.cpp#L46-L47) | `glReadBuffer(GL_FRONT)` is undefined-or-unreliable on modern double-buffered drivers (esp. with desktop compositors). Should read the back buffer before swap, or copy from the engine's final HDR target. |
| C6 | [environment_inspector.cpp:476-477](src/editor/panels/environment_inspector.cpp#L476-L477) | `ImGui::Checkbox(name.data(), ...)` where `name` is a `std::string_view`. Relies on the implementation detail that `RenderSystem::passName` returns a view into a `std::string` (null-terminated). If the return type ever becomes a true view, UB. Either change the API to `const std::string&` or copy. |
| C7 | [scene_io_controller.cpp:67-77](src/editor/framework/scene_io_controller.cpp#L67-L77) | Selection restore by slot index: after a load, if the same slot happens to be live in the new scene, the new occupant is silently selected. Selection should not survive across scene loads. |
| C8 | [editor_keybinds.h:37](src/editor/input/editor_keybinds.h#L37) + [editor_shortcuts.cpp:26-28](src/editor/framework/editor_shortcuts.cpp#L26-L28) | `EditorKeybinds::toggleEditor = F5` exists, is shown in Preferences, can be rebound — but is **never read**. The actual toggle uses raw GLFW + a hardcoded `GLFW_KEY_F5`. The rebind UI lies. |
| C9 | [editor_panel_resize.cpp:55-61](src/editor/framework/editor_panel_resize.cpp#L55-L61) | Resize clamps each panel independently but doesn't clamp `leftW + rightW < workW - minCenter`. With both side panels at max + a narrow window, the centre viewport disappears. |
| C10 | [viewport_overlay.cpp:158-160](src/editor/overlays/viewport_overlay.cpp#L158-L160) | Nav gizmo hit-tests `ImGui::GetMousePos()` unconditionally — it lights up and triggers tooltips even when the cursor is over the Inspector. Gate on `state.viewportHovered`. |
| C11 | [bottom_panel.cpp:149-153](src/editor/panels/bottom_panel.cpp#L149-L153) | `moveDot` matches keyframes by **exact float time equality**. Subject to silent drift after enough drags. Track the active keyframe by index, not time. |
| C12 | [editor_actions.cpp:7](src/editor/input/editor_actions.cpp#L7) | `#include "core/engine.h"` is unused — leftover from the `Engine::get()` purge. Dead include. |
| C13 | [viewport_toolbar.cpp:97](src/editor/overlays/viewport_toolbar.cpp#L97) | Screenshot button captures the full window from inside the UI pass — the screenshot includes the editor UI. The `screenshot.h` doc-comment even warns about this; the button still does it. |
| C14 | [environment_inspector.cpp:216-218](src/editor/panels/environment_inspector.cpp#L216-L218) | `m_hdrPathBuf` is re-`snprintf`'d from the config **every frame**, blowing away any in-flight edit unless the user has just hit Apply/Enter. Same for `m_lutPathBuf`. Sync only on entity-selection change. |
| C15 | [scene_io_controller.cpp:83-86](src/editor/framework/scene_io_controller.cpp#L83-L86) | Active-camera rebind picks the **first** `Camera::active==true` it sees. If two are active, behavior is order-dependent. Should be a single-source-of-truth invariant. |

## 3. Design / Architecture

| # | Where | Issue |
|---|---|---|
| D1 | [editor_state.h](src/editor/framework/editor_state.h) | `EditorState` is a 60-field mega-struct mixing selection, layout, gizmo config, snap, keybinds, recent scenes, **request flags** (`requestModelImport`), and **transient frame flags** (`viewportHovered`). The request-flag pattern is command-pattern-in-a-bool; a tiny intent queue (`std::vector<EditorIntent>`) would scale better and make ordering explicit. |
| D2 | [editor_context.h](src/editor/framework/editor_context.h) | `EditorContext` exposes RenderSystem, VisibilitySystem, EventSystem, CameraController to **every** panel. Most panels need only Scene+State. A two-tier split (`PanelContext` with just scene/state; `EngineContext` for the few that need rendering internals) would tighten coupling and let panels be unit-tested. |
| D3 | [gizmo_overlay.cpp:39-53](src/editor/overlays/gizmo_overlay.cpp#L39-L53) | The transform gizmo manually builds a "remap" projection matrix to compensate for the 3D pass covering the **whole GLFW window** while the viewport is a child rect. This is fragile cross-cutting coupling: change the 3D pass viewport assumption and the gizmo silently misaligns. Rendering the scene into a viewport-sized FBO would delete this whole block. |
| D4 | [hierarchy_panel.cpp:62-74](src/editor/panels/hierarchy_panel.cpp#L62-L74) | Cache invalidation by `state.hierarchyDirty || count != m_lastEntityCount`. An "add then remove same frame" leaves count unchanged → stale roots. Subscribe to ECS structural events (Scene already emits at the right places) for free correctness. |
| D5 | [editor_settings.cpp:63-81](src/editor/framework/editor_settings.cpp#L63-L81) | 18 keybinds listed by name **four** times: keybind struct, `load`, `save`, Preferences UI, plus the shortcut dispatcher. A registry pattern (`{name, action_id, default}` table) collapses to one place. |
| D6 | [editor_system.cpp:18-31](src/editor/editor_system.cpp#L18-L31) | 5-arg constructor already at the smell threshold. As you add Undo, Profiler, Scripting, this grows. A small `EditorDeps` struct passed by value-or-ref-bundle would future-proof. |
| D7 | [scene_io_controller.cpp:80-98](src/editor/framework/scene_io_controller.cpp#L80-L98) | Controller takes EventSystem + CameraController + RenderSystem direct refs and also emits `SceneLoadedEvent`. So the editor's own post-load work runs as direct calls while *other* listeners go via the event. Either go pure event-driven everywhere or document why this exception exists (the inline comment says "we just do them directly" but doesn't justify the split). |
| D8 | [editor_menu_bar.h:26](src/editor/framework/editor_menu_bar.h#L26) | `EditorMenuBar` owns `ModelImportDialog`. The dialog is unrelated to the menu — Hierarchy and Inspector empty-state both raise the same `requestModelImport` flag. The dialog should live at `EditorSystem` (or framework) level, not be a menu-bar member by accident. |
| D9 | [material_editor.cpp:37-181](src/editor/panels/material_editor.cpp#L37-L181) | `drawMaterialBodyImpl` is a free function in an anonymous namespace called only by `MaterialEditorPanel::drawMaterialBody`. The indirection adds nothing — inline it or make it a real shared helper. |
| D10 | [inspector_panel.cpp:218-253](src/editor/panels/inspector_panel.cpp#L218-L253) | `pickAsset` is a deeply generic lambda with ADL templates (`resources.template forEachOfType<Asset>`). Two non-template overloads (`pickMesh`, `pickMaterial`) would be a third as long and immediately readable. |
| D11 | [editor_actions.h:52-57](src/editor/input/editor_actions.h#L52-L57) | `EditorActions` is a namespace of free functions, but `ModelImportDialog` is a class **inside** that namespace. Mixed paradigm. Move the dialog to `framework/model_import_dialog.{h,cpp}`. |
| D12 | [editor_system.cpp:193-275](src/editor/editor_system.cpp#L193-L275) | `drawWorkspace` is the most fragile layout code in the editor: hand-computed `centerW = workW - leftW - rightW` with no min-clamp, hard-coded paddings, manual `ChildBg` push/pop because of overlay transparency. ImGui's **Docking** branch would replace all of it with `DockBuilder` and give us tabs + detach-to-window for free. |
| D13 | [gizmo_overlay.cpp:146-164](src/editor/overlays/gizmo_overlay.cpp#L146-L164) | `projectToViewport` takes `vpMin` and `vpSize` parameters it doesn't use (cast to `(void)`). Dead-by-design — they're there because the function used to need them. Strip. |
| D14 | [transform_gizmo.cpp:158-163](src/editor/gizmo/transform_gizmo.cpp#L158-L163) | `GizmoElement::Screen` enum value is defined and `getDragPlaneNormal` handles it — but no hit-test ever returns it and no drawing draws it. Either implement screen-aligned drag or remove the enum value. |
| D15 | [transform_gizmo_draw.cpp:158-163](src/editor/gizmo/transform_gizmo_draw.cpp#L158-L163) | `drawScaleGizmo` draws a "Center box (uniform scale)" visual handle — but `hitTestScale` never tests it. The handle is **decorative**. Either wire uniform scale or stop drawing it. |
| D16 | [editor_system.cpp:64-191](src/editor/editor_system.cpp#L64-L191) | `update()` does input gating, F5 toggle, title sync, both ImGui frame begin paths (hidden and visible), the main draw, and the floating windows. It's well-commented but already 130 lines of pure orchestration. Splitting `update` into `tickInput` + `drawFrame` would make the hidden/visible asymmetry less subtle. |
| D17 | [editor_panel_resize.cpp:11-66](src/editor/framework/editor_panel_resize.cpp#L11-L66) | `Layout` is built from EditorState fields in `drawWorkspace`, then passed back into `EditorPanelResize::process`, which mutates EditorState. The detour through `Layout` is pointless when the resizer could read EditorState directly. The wrapper struct made sense if the layout numbers were ephemeral — but they live in EditorState. |
| D18 | [hierarchy_panel.cpp:225](src/editor/panels/hierarchy_panel.cpp#L225) | Unparent in the hierarchy ctx menu calls `state.markSceneDirty()` but the equivalent action in `InspectorPanel::drawHierarchySection` lines 469-473 also does it. Six other places do the same dance (`HierarchyOperations::markDirty + state.hierarchyDirty + state.markSceneDirty`). One helper (`commitHierarchyChange(scene, state, id)`) would dedupe and guarantee no future caller forgets one of the three. |
| D19 | [environment_inspector.cpp:497-535](src/editor/panels/environment_inspector.cpp#L497-L535) | `EnvironmentInspector::draw` is 537 loc — the largest single file in the editor. Logical: lighting, camera, post, scene, pipeline, plus preset bar, plus picker plumbing. Tab bar (Lighting / Post / Pipeline) would shrink each scroll session and visually match Preferences. |
| D20 | [inspector_panel.cpp:300-410](src/editor/panels/inspector_panel.cpp#L300-L410) | Light / Camera / Animation section bodies are written verbosely. A property-row helper (`prop("Intensity").drag(&l.intensity, 0.5f, 0.0f, 100000.0f)`) would halve the repetition and centralize the dirty-mark. |

## 4. Quality nits

- [editor_actions.cpp:88,126,142,189,265](src/editor/input/editor_actions.cpp#L88) — indentation of `state.markSceneDirty()` is misaligned (looks like a Find/Replace artefact).
- [editor_widgets.cpp:100-104](src/editor/ui/editor_widgets.cpp#L100-L104) — `thread_local` static stack inside an accessor is heavier than needed; cards never nest, a single object would do.
- [environment_inspector.cpp:131-145](src/editor/panels/environment_inspector.cpp#L131-L145) — `detectPreset` matches by direct equality on floats and bools; brittle to any future preset tuning. A "fuzzy-match within ε" helper would survive bloom-value refactors.
- [editor_widgets.cpp:281-290](src/editor/ui/editor_widgets.cpp#L281-L290) — `iconPaddedLabel` inserts space characters until the icon-width matches; works, but breaks when the user picks a proportional font with a different space width. Could use `ImGui::Indent` instead.

---

## 5. Things we want (feature gaps)

These are not bugs — they're capability gaps separating "modern editor scaffolding"
from "long-term, best-of-breed engine editor."

### Tier 1 — must-haves for a long-term editor

1. **Undo / redo.** No command stack today; every mutation marks dirty and is
   irreversible. This is the single biggest gap.
2. **Multi-selection.** Single `EntityId` only. Inspector, gizmo, actions all
   assume one entity. Industry-standard for transform bulk-ops.
3. **Prefab / nested scene system.** Import-model exists, but no
   "make-subtree-reusable" or "instantiate prefab into scene" flow.
4. **Save-on-quit prompt + autosave + crash recovery.** Today: window close
   silently drops a dirty scene; only the title `*` warns.
5. **Console / log panel.** Logger goes to disk; the editor has no in-app view.
6. **Profiler panel** with frame-time breakdown by render pass / system.
7. **Copy / paste / duplicate-array.** Only "Duplicate" exists. No clipboard.
8. **Switch to ImGui Docking branch** for real dockable panels (tabs,
   splitters, detach-to-window, save/load layouts).
9. **Tests.** Pure UI is hard to test, but gizmo math, AssetPicker, EulerCache,
   SceneIOController, and bounds/picking all could.

### Tier 2 — high impact for a daily editor

10. **Render scene to viewport-sized FBO** (eliminates gizmo remap matrix,
    enables in-panel scene rendering, simplifies post-bloat).
11. **Drag-and-drop from OS filesystem** into viewport / asset browser.
12. **Drag-and-drop between panels** (drag a material onto an entity in the
    viewport).
13. **Asset Browser filtering / search.** Today: flat grid only.
14. **Asset thumbnail disk cache** — survives restart; first paint of a 500-
    material project is currently slow.
15. **Validate Recent Scenes against filesystem.** Deleted files still show.
16. **Fuzzy filter in Hierarchy** (Sublime-style); current is naive substring.
17. **Command palette (Ctrl+P)** — "goto entity", "open panel", "run action".
18. **Snap-to-ground / snap-to-vertex.** Currently only grid snap.
19. **Curve editor for animation.** Bezier curves, per-segment easing.
20. **"Look through camera"** quick-action on Camera components.
21. **Lock affordance** (lock pick, lock transform, lock visibility) per entity.
22. **Material Editor: HDRI swap, multi-shape compare**, "show channels"
    debug.
23. **Sky / atmosphere / fog editor** (currently only IBL HDR).
24. **Area / volume / IES light gizmos** (only Dir/Point/Spot rendered).
25. **Measurement / ruler / scene-units overlay.**

### Tier 3 — extensibility & polish

26. **ENABLE_EDITOR build switch.** Runtime ship without editor.
27. **Plugin / scripting hooks** — register custom panel, custom inspector
    section for a custom component, custom action.
28. **Keymap registry** (single source of truth; rebind UI, default load/save,
    dispatcher all key off it).
29. **Localization framework** (strings via lookup).
30. **High-contrast theme variant + accessibility annotations.**
31. **Per-scene layout** override (some scenes want a wider Inspector).
32. **Theming the gizmo** (currently hard-coded EditorStyle constants;
    AXIS_X_U32 etc. inlined into TransformGizmo via `constexpr` so a runtime
    theme switch doesn't reach the gizmo).
33. **Live shader edit panel** — file watcher already exists in the engine.

### Tier 4 — workflow

34. **Bookmarks / saved cameras** ("save view 1", "F2 to recall").
35. **Hierarchy: collapse all / expand all / find selection.**
36. **Animation: import from glTF, retarget, blend trees.**
37. **Scene tagging / collections** (Unity-style layer/tag system).
38. **In-editor build/play mode.** No play-mode separation today; the engine
    is always "running."

---

## 6. Bottom line

The structural refactor was a clean win: the editor is foldered, the god-file
is dead, panels are uniform, and the Material Editor / Asset Browser /
Environment Inspector are real features rather than placeholders. The
**framework layer** (context / state / settings / sceneIO / picker / shortcuts /
resize) is genuinely well-shaped.

The next round of work splits into three buckets:

- **Tighten correctness** — fix C1 (selection ≠ dirty), C2 (scale decompose),
  C8 (F5 keybind lie), C9 (resize clamp), C13 (screenshot includes UI).
  These are small, high-confidence patches.
- **Tighten coupling** — D2 (split EditorContext), D5 (keybind registry),
  D12 (Docking branch), D18 (one-helper-per-mutation). These are
  architectural moves, each ~1-2 days.
- **Close feature gaps** — Tier 1 (undo, multi-select, prefabs, autosave,
  console, profiler) is the actual "best-of" work. Without these, the
  editor is "modern" but not "long-term."
