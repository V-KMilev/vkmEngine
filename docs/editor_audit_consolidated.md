# Editor — Consolidated Audit (best-of both passes)

> Merges and deduplicates [docs/editor_refactor_audit.md](editor_refactor_audit.md)
> (prior pass, dated 2026-05-21) and
> [docs/editor_independent_audit.md](editor_independent_audit.md)
> (this pass, file-by-file blind walk).
>
> Where both passes converged, the consolidated version cites both. Where one
> pass caught something the other missed, it's tagged `[indep]` or `[prior]`
> for traceability.
>
> Scope: `src/editor/**`. Branch: `vkm/dev/editor/refactor`. ~8.1k loc, 54 files.

---

## 1. State of the refactor

Three sequential phases have landed:

- **Phase A — godfile decomposition** (`2145759`, `51411bd`, `950489b`):
  the old monolithic `editor_system.cpp` (~550 loc) became a ~280 loc shell
  with [scene_io_controller](../src/editor/framework/scene_io_controller.cpp),
  [editor_menu_bar](../src/editor/framework/editor_menu_bar.cpp),
  [editor_shortcuts](../src/editor/framework/editor_shortcuts.cpp),
  [editor_status_bar](../src/editor/framework/editor_status_bar.cpp), and
  [editor_panel_resize](../src/editor/framework/editor_panel_resize.cpp)
  carved out into framework units.
- **Phase B — `Engine::get()` purge** (`ed41537`, `726b870`): scene IO and
  panels no longer reach a global singleton. Everything flows through
  [`EditorContext& ec`](../src/editor/framework/editor_context.h).
- **Phase C — "full modernization pass"** (`96e7d12`, +8147 lines):
  `EditorPanel` base class deleted, panels became POD draw-objects; gizmos
  split 3-way; five duplicated file pickers merged into
  [`AssetPicker`](../src/editor/framework/asset_picker.cpp); settings
  persistence, dirty tracking, MRU, screenshot, light/camera viewport gizmos,
  animation timeline, Frame-All, F5 hide-overlay.

### What's genuinely good

1. **God-file decomposition is real.** `EditorSystem` is a thin orchestrator;
   menu / status / shortcuts / resize / sceneIO are independent units.
2. **`EditorContext` is a clean per-frame DI bundle.** Every panel has one
   signature: `void draw(EditorContext&)`. No `Engine::get()` smell.
3. **Editor-rooted includes everywhere** (`#include "framework/foo.h"`).
4. **`AssetPicker` is reusable & cached** — every dialog (Save-As, Load, IBL,
   LUT, PBR folder, texture slot) goes through it.
5. **`EulerCache<Key>` solves a real gimbal-lock UX bug** in one small
   helper, reused by Inspector and Animation editor.
6. **Status bar shows a parent breadcrumb** (deep selection: "Root > Group >
   Leaf").
7. **F5 hide uses raw GLFW intentionally and documents why.**
8. **`materialPreviewTexture` is budgeted** — Asset Browser doesn't stall on
   first paint of a big grid.
9. **Selection-aware affordances** ripple correctly across toolbar / menu /
   context menus / inspector empty state.
10. **Theme + style split:** `editor_theme.cpp` configures ImGui;
    `editor_style.h` exposes constants (limited — see issue Q1).

The **structural** refactor is done. What's left is correctness, polish, and
real capability gaps.

---

## 2. Correctness bugs (fix these)

Marked `[both]`, `[prior]`, or `[indep]` for traceability. Severity ordering.

### 2.1 Data-loss vectors

| # | Where | Issue | Source |
|---|---|---|---|
| B1 | [scene_io_controller.cpp:41](../src/editor/framework/scene_io_controller.cpp#L41) | `SceneSerializer::save()` return value is ignored; `state.sceneDirty = false` runs unconditionally. A failed save (disk full, permission, locked file) silently looks like success. | `[prior]` |
| B2 | [scene_io_controller.cpp:67-72](../src/editor/framework/scene_io_controller.cpp#L67-L72) | Selection is cleared **before** the load is attempted; on load failure, selection is not restored. Typing a bad path silently drops the selection. | `[prior]` |
| B3 | [scene_io_controller.cpp:67-77](../src/editor/framework/scene_io_controller.cpp#L67-L77) | Selection restore by **slot index alone**: after a successful load, if the same slot happens to be live in the new scene, the new occupant is silently selected — wrong identity. | `[indep]` |
| B4 | [scene_io_controller.cpp:61](../src/editor/framework/scene_io_controller.cpp#L61) | Hand-rolled `APP_ROOT_DIR + "/scenes/scene.json"`; no `create_directories()` for the scenes dir on first save. | `[prior]` |
| B5 | No autosave, no `.bak` rotation, no overwrite-protection if the file changed on disk. | `[prior]` |
| B6 | [editor_settings.cpp](../src/editor/framework/editor_settings.cpp) | Settings JSON has no version key (first incompatible rename wipes bindings); writes directly over the file (no temp+rename atomicity); catches all JSON exceptions silently. | `[prior]` |
| B7 | [editor_system.cpp](../src/editor/editor_system.cpp) | No save-on-quit prompt. Closing the window with a dirty scene silently drops it; only the title `*` warns. | `[indep]` |

### 2.2 Dirty-flag coverage (whole panels never call `markSceneDirty()`)

Grep tally — 23 calls across 6 files. Three panels and one core overlay
**don't call it at all**:

| File | `markSceneDirty()` calls | Verdict |
|---|---|---|
| [material_editor.cpp](../src/editor/panels/material_editor.cpp) | **0** | every PBR edit is silent |
| [bottom_panel.cpp](../src/editor/panels/bottom_panel.cpp) | **0** | animation keyframe edits silent |
| [asset_browser.cpp](../src/editor/panels/asset_browser.cpp) | **0** | material/mesh assign silent |
| [environment_inspector.cpp](../src/editor/panels/environment_inspector.cpp) | 2 | most fields silent |
| [inspector_panel.cpp](../src/editor/panels/inspector_panel.cpp) | 12 | best coverage |
| [hierarchy_panel.cpp](../src/editor/panels/hierarchy_panel.cpp) | 2 | partial |
| [gizmo_overlay.cpp](../src/editor/overlays/gizmo_overlay.cpp) | 2 | needs re-audit (see B8) |
| [editor_actions.cpp](../src/editor/input/editor_actions.cpp) | 4 | entity ops covered |

Spot-confirmed silent mutations (`[prior]`):
- [inspector_panel.cpp:95](../src/editor/panels/inspector_panel.cpp#L95) — `Name` component add.
- [asset_browser.cpp:146](../src/editor/panels/asset_browser.cpp#L146) — material assignment via context menu.
- [asset_browser.cpp:198](../src/editor/panels/asset_browser.cpp#L198) — mesh assignment via context menu.

Two cases of the **opposite** problem, where `markSceneDirty()` runs on a
**read-only** action `[indep]`:

| # | Where | Issue |
|---|---|---|
| B8 | [gizmo_overlay.cpp:548-549](../src/editor/overlays/gizmo_overlay.cpp#L548-L549) | `handleViewportPick` marks scene dirty and sets `hierarchyDirty` on a **selection**. Selection isn't saved; user is prompted to save just for clicking. |
| B9 | [bottom_panel.cpp:58-60](../src/editor/panels/bottom_panel.cpp#L58-L60) | Animation preview pose writes back into the live `Transform` without marking dirty AND without tracking play-mode vs. edit-mode — the preview can permanently mutate the scene if the user saves while previewing. |

The Phase C commit message claims "every mutator calls `state.markSceneDirty()`."
By a wide margin, it doesn't.

### 2.3 Gizmo bugs

| # | Where | Issue | Source |
|---|---|---|---|
| B10 | [transform_gizmo_drag.cpp:75](../src/editor/gizmo/transform_gizmo_drag.cpp#L75) | **Scale snap never reads `state.snapScale`.** The field exists in `EditorState`, the Preferences UI edits it, the serializer saves it — the gizmo never consumes it. Rotation snap works (delta-based, in `m_snapAngle`); scale does not. | `[prior]` |
| B11 | [transform_gizmo_drag.cpp:78-94](../src/editor/gizmo/transform_gizmo_drag.cpp#L78-L94) | `handleScaleDrag` calls `glm::decompose` on the start matrix **every frame**. (a) Discards skew — sheared models silently lose shear after a scale drag. (b) For Transforms with non-uniform scale + rotation, decompose can return a quaternion that flips mid-drag — exactly the gimbal-lock hazard the rotation path is designed to avoid. Cache `start{Pos,Rot,Scale}` once at drag-start. | `[both]` |
| B12 | [transform_gizmo.cpp](../src/editor/gizmo/transform_gizmo.cpp) | **Orthographic camera path is broken.** `m_screenFactor = GIZMO_SIZE_PIXELS / ndcLength` assumes perspective; under ortho the gizmo scales with object distance. | `[prior]` |
| B13 | [transform_gizmo.cpp](../src/editor/gizmo/transform_gizmo.cpp) | `AXIS_HIT_RADIUS = 10.0f` is not DPI-aware. | `[prior]` |
| B14 | [transform_gizmo.cpp:286-287](../src/editor/gizmo/transform_gizmo.cpp#L286-L287) | `getDragRotation()` returns the last value set by `handleRotationDrag` — cleared only on rotation **release**. Stale across non-rotate frames. Caller is safe today; brittle. | `[indep]` |
| B15 | [transform_gizmo.cpp](../src/editor/gizmo/transform_gizmo.cpp) | Behind-camera cull only checks origin's `clip.w`. Arrow head, center dot, ring alpha clamp colors and pixel sizes are hardcoded outside `EditorStyle`. | `[prior]` |
| B16 | [transform_gizmo_hit.cpp](../src/editor/gizmo/transform_gizmo_hit.cpp) | Rotation ring hit-test projects twice per frame; plane-quad hit test is fragile at near-parallel screen-space axes. | `[prior]` |
| B17 | [transform_gizmo.cpp:19-24](../src/editor/gizmo/transform_gizmo.cpp#L19-L24) + [transform_gizmo.cpp:158-163](../src/editor/gizmo/transform_gizmo.cpp#L158-L163) | `GizmoElement::Screen` enum value is defined and `getDragPlaneNormal` handles it — but no hit-test ever returns it, no draw ever draws it. Dead code (or unimplemented feature). | `[indep]` |
| B18 | [transform_gizmo_draw.cpp:158-163](../src/editor/gizmo/transform_gizmo_draw.cpp#L158-L163) | `drawScaleGizmo` draws a "center box (uniform scale)" — but `hitTestScale` never tests it. The center handle is **decorative**. | `[indep]` |
| B19 | [transform_gizmo_draw.cpp](../src/editor/gizmo/transform_gizmo_draw.cpp) | Orphan `#include <algorithm>` (abandoned mid-edit). Revert or finish. | `[prior]` |

### 2.4 Other correctness issues

| # | Where | Issue | Source |
|---|---|---|---|
| B20 | [screenshot.cpp:45-46](../src/editor/framework/screenshot.cpp#L45-L46) | (a) Sets `GL_PACK_ALIGNMENT` and `GL_READBUFFER` without restoring them — GL state leak into the next frame's render path. (b) `glReadBuffer(GL_FRONT)` is undefined-or-racy on modern double-buffered drivers (compositors); read back-buffer pre-swap, or copy from the engine's final HDR target. | `[both]` |
| B21 | [viewport_toolbar.cpp:97](../src/editor/overlays/viewport_toolbar.cpp#L97) | Screenshot button captures the full window from inside the UI pass — the screenshot includes the editor UI. `screenshot.h`'s own doc-comment warns; the toolbar does it anyway. | `[indep]` |
| B22 | [editor_keybinds.h:37](../src/editor/input/editor_keybinds.h#L37) + [editor_shortcuts.cpp:26-28](../src/editor/framework/editor_shortcuts.cpp#L26-L28) | `EditorKeybinds::toggleEditor = F5` exists, is shown in Preferences, can be rebound — but is **never read**. The actual F5 path uses raw GLFW with a hardcoded key. **The rebind UI lies.** | `[indep]` |
| B23 | [editor_panel_resize.cpp:55-61](../src/editor/framework/editor_panel_resize.cpp#L55-L61) | Clamps each panel independently but doesn't enforce `leftW + rightW < workW - minCenter`. Narrow window + both panels at max → centre viewport disappears. | `[indep]` |
| B24 | [editor_panel_resize.cpp](../src/editor/framework/editor_panel_resize.cpp) | Drag flags survive F5 hide → grabbing a splitter then hiding causes ghost-drag on re-show. | `[prior]` |
| B25 | [viewport_overlay.cpp:158-160](../src/editor/overlays/viewport_overlay.cpp#L158-L160) | Nav gizmo hit-tests `ImGui::GetMousePos()` unconditionally — lights up over Inspector. Gate on `state.viewportHovered`. | `[indep]` |
| B26 | [bottom_panel.cpp:149-153](../src/editor/panels/bottom_panel.cpp#L149-L153) | `moveDot` matches keyframes by **exact float time equality**. Drift-prone. Use index, not time. | `[indep]` |
| B27 | [gizmo_overlay.cpp](../src/editor/overlays/gizmo_overlay.cpp) | Light gizmo position falls back to local-space `tf.position` when no `WorldTransform` — wrong for parented lights. Light pick-radius hardcoded to 0.5 world units → large lights unpickable, tiny lights have oversized hit targets. `forEach<Light, Transform>` runs every frame with no frustum cull and no skip on disabled lights. | `[prior]` |
| B28 | [environment_inspector.cpp:216-218](../src/editor/panels/environment_inspector.cpp#L216-L218) | `m_hdrPathBuf` is `snprintf`'d from the config **every frame** — in-flight text edit is blown away unless Enter/Apply is pressed. Same for `m_lutPathBuf`. | `[indep]` |
| B29 | [environment_inspector.cpp:476-477](../src/editor/panels/environment_inspector.cpp#L476-L477) | `ImGui::Checkbox(name.data(), ...)` where `name` is `std::string_view`. Relies on the implementation detail that `RenderSystem::passName` returns a view into a `std::string` (null-terminated). UB-prone. | `[indep]` |
| B30 | [scene_io_controller.cpp:83-86](../src/editor/framework/scene_io_controller.cpp#L83-L86) | Active-camera rebind picks the **first** `Camera::active==true` it sees. Order-dependent if two are active. | `[indep]` |
| B31 | [asset_picker.cpp:54-57](../src/editor/framework/asset_picker.cpp#L54-L57) | Off-by-one on `maxResults`: pushes before the check, so the cap is always exceeded by one. Plus `std::error_code` passed to `directory_iterator` is never consumed — silent failure on unreadable paths. | `[both]` |
| B32 | [editor_status_bar.cpp](../src/editor/framework/editor_status_bar.cpp) | Hierarchy breadcrumb walk has no cycle guard (relies on engine-side guarantee); `char chain[192]` truncates ancestor chains with no feedback. | `[prior]` |
| B33 | [material_editor.cpp](../src/editor/panels/material_editor.cpp) | `m_pendingTexture` is a raw pointer into an asset; dangles if the asset is reloaded mid-modal. Live preview re-renders every frame (`live=true`); hardcoded texture extension lists duplicated across files. | `[prior]` |
| B34 | [environment_inspector.cpp:131-145](../src/editor/panels/environment_inspector.cpp#L131-L145) | Preset detection uses `< 5e-4f` against hardcoded preset values; brittle to future tuning. `m_iblPathMemo` etc. have no invalidation hook on external mutation. Hardcoded `SetNextItemWidth(-150.0f)` ignores `EditorStyle::LABEL_WIDTH`. | `[prior]` |
| B35 | [preferences_panel.cpp](../src/editor/panels/preferences_panel.cpp) | `m_fpsLimitEdit` not re-synced on each open; pointer-identity keybind-row tracking (`m_rebindTarget == labelLiteral`) silently breaks if labels become `std::string`. | `[prior]` |
| B36 | [editor_widgets.cpp](../src/editor/ui/editor_widgets.cpp) | `EulerCache<Key>` has no gimbal-lock fallback at pitch ≈ ±90°. `drawRemoveButton` assumes `SmallButton` is exactly 20px wide. `cardStack()` `thread_local` is fine but nothing asserts balanced begin/end. | `[prior]` |
| B37 | [editor_keybinds.cpp](../src/editor/input/editor_keybinds.cpp) | `strcat` into a 32-byte buffer in `keybindLabel`; safe in practice but unchecked. No keybind conflict detection. | `[prior]` |
| B38 | [editor_icons.cpp](../src/editor/ui/editor_icons.cpp) | Icons re-rasterized every frame (no atlas); inconsistent pixel-snapping; missing folder / file / gear / warning / eye / color-swatch / drag-grip glyphs. | `[prior]` |
| B39 | [editor_actions.cpp:7](../src/editor/input/editor_actions.cpp#L7) | `#include "core/engine.h"` is unused — leftover from the `Engine::get()` purge. | `[indep]` |
| B40 | [editor_actions.cpp](../src/editor/input/editor_actions.cpp) | Model import failure path doesn't surface error to the user. | `[prior]` |
| B41 | [gizmo_overlay.cpp:39-53](../src/editor/overlays/gizmo_overlay.cpp#L39-L53) | Viewport-pos remap math is the most fragile cross-cutting coupling in the editor (see D3 below) — incidentally causes subtle hover-pick offset bugs under non-zero `viewportPos.y`. | `[prior]` |

---

## 3. Architecture / design smells

| # | Where | Issue | Source |
|---|---|---|---|
| D1 | [editor_context.h](../src/editor/framework/editor_context.h) | **`EditorContext` is a fat aggregate** that grows linearly with engine systems. Already holds `cameraController`, `renderSystem`, `visibilitySystem`, `events`, `viewportPos/Size`. Adding any system (physics, audio, navmesh) means editing the struct and every panel header that forward-declares it. Options: typed registry (`ec.get<RenderSystem>()`), two-tier split (`PanelContext` + `EngineContext`), or per-panel constructor injection. | `[both]` |
| D2 | [editor_state.h](../src/editor/framework/editor_state.h) | **`EditorState` is a 60-field mega-struct** mixing selection, layout, gizmo config, snap, keybinds, recent scenes, **request flags** (`requestModelImport`), and **transient frame flags** (`viewportHovered`). The request-flag pattern is command-pattern-in-a-bool — a tiny intent queue would scale better and make ordering explicit. | `[indep]` |
| D3 | [gizmo_overlay.cpp:39-53](../src/editor/overlays/gizmo_overlay.cpp#L39-L53) | **The transform gizmo manually builds a "remap" projection matrix** to compensate for the 3D pass covering the whole GLFW window while the viewport is a child rect. Change the 3D pass viewport assumption and the gizmo silently misaligns. Rendering the scene into a **viewport-sized FBO** would delete this entire compensation block and unlock screenshots / multi-viewport / ID-buffer picking. | `[both]` |
| D4 | (cross-cutting) | **No editor-internal event bus.** `OnSelectionChanged`, `OnSceneSwapped`, `OnEntityCreated`, gizmo drag start/end, etc. are not broadcast. Panels poll `state.selectedEntity` and infer — that's why every panel needs `state.hierarchyDirty`-style ad-hoc flags. | `[prior]` |
| D5 | [hierarchy_panel.cpp:62-74](../src/editor/panels/hierarchy_panel.cpp#L62-L74) | Cache invalidation is `state.hierarchyDirty || count != m_lastEntityCount`. An "add then remove same frame" leaves count unchanged → stale roots. ECS structural events (when D4 lands) fix this for free. | `[indep]` |
| D6 | (cross-cutting) | **No edit-mode vs. play-mode distinction.** Animation preview writes back into the live `Transform`. No "exit play mode reverts changes" model. The Playback Bar exists; the semantic doesn't. | `[prior]` |
| D7 | [inspector_panel.cpp:114-119](../src/editor/panels/inspector_panel.cpp#L114-L119) + [editor_settings.cpp:63-81](../src/editor/framework/editor_settings.cpp#L63-L81) | **Hardcoded `if (scene.has<X>)` dispatch** in the Inspector means adding a new component requires editing the Inspector. **Eighteen keybinds listed four times** (struct + load + save + Preferences UI). Both want the same shape of fix: a registry (`name() / category() / draw()` per type, `{name, action_id, default}` per binding). | `[both]` |
| D8 | (cross-cutting) | **`EnvironmentConfig` singleton coupling.** Several panels reach for the Environment entity directly. It's a singleton-in-disguise; per-camera or per-scene environments are blocked. | `[prior]` |
| D9 | [scene_io_controller.cpp:80-98](../src/editor/framework/scene_io_controller.cpp#L80-L98) | Controller takes EventSystem + CameraController + RenderSystem direct refs AND emits `SceneLoadedEvent`. Editor's own post-load runs as direct calls while other listeners go via the event. Dual signalling. | `[indep]` |
| D10 | [editor_system.cpp:18-31](../src/editor/editor_system.cpp#L18-L31) | 5-arg constructor already at the smell threshold. As you add Undo, Profiler, Scripting deps, this grows. A small `EditorDeps` aggregate would future-proof. | `[indep]` |
| D11 | [editor_menu_bar.h:26](../src/editor/framework/editor_menu_bar.h#L26) | `EditorMenuBar` owns `ModelImportDialog` by historical accident — the dialog is unrelated to the menu. Hierarchy and Inspector empty-state both raise `requestModelImport`. Lift the dialog to `EditorSystem` (or `framework/`). | `[indep]` |
| D12 | [editor_actions.h:52-57](../src/editor/input/editor_actions.h#L52-L57) | `EditorActions` is a free-function namespace, but `ModelImportDialog` is a **class inside** that namespace. Mixed paradigm. | `[indep]` |
| D13 | [material_editor.cpp:37-181](../src/editor/panels/material_editor.cpp#L37-L181) | `drawMaterialBodyImpl` is a free function in an anonymous namespace called only by `MaterialEditorPanel::drawMaterialBody`. Indirection adds nothing. | `[indep]` |
| D14 | [editor_panel_resize.cpp:11-66](../src/editor/framework/editor_panel_resize.cpp#L11-L66) | `Layout` is built from EditorState fields in `drawWorkspace`, passed back into the resizer, which mutates EditorState. The intermediary struct is pointless when the resizer could read EditorState directly. | `[indep]` |
| D15 | (cross-cutting) | **Six near-identical "markDirty + hierarchyDirty + markSceneDirty" sequences** in hierarchy / inspector / gizmo paths. One helper (`commitHierarchyChange(scene, state, id)`) would dedupe AND guarantee no future caller forgets one of the three. | `[indep]` |
| D16 | [gizmo_overlay.cpp:146-164](../src/editor/overlays/gizmo_overlay.cpp#L146-L164) | `projectToViewport` takes `vpMin` and `vpSize` parameters it doesn't use (`(void)`-cast). Dead. | `[indep]` |
| D17 | [editor_system.cpp:193-275](../src/editor/editor_system.cpp#L193-L275) | `drawWorkspace` arithmetic is brittle (hand-computed `centerW = workW - leftW - rightW` with no clamp; hardcoded paddings). **ImGui Docking branch** would replace it with `DockBuilder` + tabs + detach-to-window + per-scene layouts. | `[both]` |
| D18 | [bottom_panel.cpp:34-356](../src/editor/panels/bottom_panel.cpp#L34-L356) | `drawAnimationSection` is ~320 loc (longest function in the editor). Magic literals (laneH 16, rulerH 18, ticks 10). | `[prior]` |
| D19 | [environment_inspector.cpp](../src/editor/panels/environment_inspector.cpp) | 537 loc — the largest single file in the editor. Should tab-bar by section (Lighting / Camera / Post / Pipeline) to match Preferences. | `[indep]` |
| D20 | (cross-cutting) | **No render-to-texture viewport target.** The viewport area is a "hole" in ImGui with the main framebuffer behind it. That's why screenshot uses `glReadBuffer(GL_FRONT)` (B20) and the gizmo needs the remap matrix (D3). Same fix unlocks both. | `[both]` |
| D21 | (cross-cutting) | **Resource handles held as raw pointers in modal state** (`m_pendingTexture` is the obvious one; the pattern repeats). Generational handles would survive resource reloads. | `[prior]` |
| D22 | (cross-cutting) | **No editor tests.** `tests/editor/` doesn't exist. 8k loc of new code has no regression net. | `[prior]` |
| D23 | (cross-cutting) | **Zero `TODO/FIXME/HACK` comments** across `src/editor/`. Either suspiciously polished or scrubbed pre-commit. Some real "fix later" notes are healthy. | `[prior]` |
| D24 | (cross-cutting) | **No DPI scaling pipeline anywhere.** `26.0f` is a constant, not `26.0f * dpiScale`. `EditorStyle::*` likewise. | `[prior]` |
| D25 | [editor_style.h](../src/editor/ui/editor_style.h) | Token surface too thin (`LABEL_WIDTH`, `TITLE_SCALE` only). Button sizes 26/32/56, card indent 14, pad (8,7), spacing 6/9/14/18 scattered as literals. | `[prior]` |
| D26 | [editor_theme.cpp](../src/editor/ui/editor_theme.cpp) | No runtime theme switch / hot reload; `DragDropTarget` color is a literal. | `[prior]` |
| D27 | [inspector_panel.cpp:218-253](../src/editor/panels/inspector_panel.cpp#L218-L253) | `pickAsset` is a deeply generic lambda with ADL templates (`resources.template forEachOfType<Asset>`). Two non-template overloads would be a third as long and immediately readable. | `[indep]` |
| D28 | [inspector_panel.cpp](../src/editor/panels/inspector_panel.cpp) | Light/Camera/Animation section bodies are verbose. A property-row builder (`prop("Intensity").drag(&l.intensity, 0.5f, 0.0f, 100000.0f)`) would halve repetition. | `[indep]` |
| D29 | (cross-cutting) | **Submodule state** at audit time: `modules/vkmGL` is on `heads/major-update-dev` (a feature branch, not master) with uncommitted changes inside `glew`, `glm`, `vkmLog`. The editor refactor isn't `master`-tracked in vkmGL. Easy to ship the editor refactor with an unintended GL/glm/vkmLog bump. **Resolve before merging the editor branch.** | `[prior]` |

---

## 4. Feature gaps — what we want next

Tier 1 = blocks the editor from being "long-term best-of-breed."
Tier 2 = daily-driver UX.
Tier 3 = workflow / polish.
Tier 4 = extensibility.

### 4.1 Tier 1 — must-haves

1. **Undo / Redo (Command pattern).** Single largest missing feature. Once
   it exists, the dirty-tracking gaps in §2.2 become irrelevant (commands
   flip dirty automatically), bulk operations get transactional grouping,
   and asset-browser / material-editor edits become recoverable. `[both]`
2. **Multi-selection.** `EditorState::selectedEntity` is a single
   `EntityId`. Promote to `std::vector<EntityId>` or a `SelectionSet`. The
   Inspector, gizmo, and every action assume one entity. `[both]`
3. **Atomic save + autosave + `.bak` rotation + crash recovery.** `[both]`
4. **Surface save/load failures to the user** (toast / log strip). `[prior]`
5. **Prefab / nested-scene system.** Import-model exists; no
   "make-subtree-reusable" or "instantiate prefab" flow. `[both]`
6. **Console / log panel** — engine log viewer with severity filter +
   search. Currently Logger goes to disk; the editor has no in-app view. `[both]`
7. **Profiler tab** — frame-time breakdown by system + GPU pass. `[both]`
8. **Render-to-texture viewport target** (closes D3, D20, B20, B21,
   enables ID-buffer picking, thumbnails, multi-viewport, HDR-correct
   screenshots). One change pays back four issues. `[both]`
9. **Editor tests scaffold** (settings round-trip, selection lifetime
   through scene load, gizmo math, dirty-flag coverage matrix, keybind
   dispatch). `[both]`
10. **Settings JSON versioning + atomic write.** `[prior]`

### 4.2 Tier 2 — daily-driver UX

11. **Switch to ImGui Docking branch** (tabs, splitters, detach, named
    layouts: "Animator" / "Lighting" / "Default"). `[both]`
12. **Drag-and-drop from OS filesystem** into viewport / asset browser. `[both]`
13. **Drag-and-drop between panels** (material → entity in viewport;
    asset → inspector slot). `[both]`
14. **Asset Browser: filter / search / pagination / virtualization.** `[both]`
15. **Asset thumbnail disk cache** (survives restart). `[indep]`
16. **Validate Recent Scenes against filesystem** — drop deleted files. `[indep]`
17. **Inline rename in Hierarchy** (F2 / double-click). `[prior]`
18. **Keyboard nav in Hierarchy** (arrows, Enter rename, Delete delete,
    Ctrl+D duplicate). `[prior]`
19. **Fuzzy filter in Hierarchy** (currently naive substring); **command
    palette** (Ctrl+P) for "goto entity", "open panel", "run action". `[both]`
20. **Copy / paste components** (Unity/Unreal idiom — right-click-on-label
    "Paste value"). `[prior]`
21. **Locked inspector** (pin the current entity). `[prior]`
22. **Add-Component registry** to replace hardcoded `if (scene.has<X>)`
    dispatch. `[prior]`
23. **Per-field Revert / Reset-to-default.** `[prior]`
24. **Searchable enum dropdowns** (long enums like `RenderMode` benefit
    immediately). `[prior]`
25. **Scale snap** (close B10), **plane handles for scale**, **screen-space
    uniform scale handle** (close B18), **angle readout while rotating**,
    **distance readout while translating**. `[prior]`
26. **Marquee (rubber-band) selection** in the viewport. `[prior]`
27. **Snap-to-vertex / snap-to-grid / snap-to-face** for translation. `[both]`
28. **Click-through** to pick the entity behind the current selection. `[prior]`
29. **Curve editor** alongside the keyframe dope-sheet. `[both]`
30. **Material Editor: HDRI swap, multi-shape compare, "show channels"** debug
    overlay. `[indep]`

### 4.3 Tier 3 — polish & workflow

31. **DPI scale pipeline** (single multiplier applied to all `EditorStyle::*`
    and icon sizes). `[prior]`
32. **Theme tokens for spacing/padding/button sizes** — kill the literals. `[prior]`
33. **Theme hot-reload / runtime theme switch.** `[prior]`
34. **Markdown / rich tooltips.** `[prior]`
35. **Keyboard navigation across custom widgets** (icon rows, hierarchy,
    asset grid). `[prior]`
36. **Keybind conflict detection** in Preferences. `[prior]`
37. **Per-context keybind layers** (viewport vs. inspector vs. hierarchy). `[prior]`
38. **Mouse-button bindings**, not just keys. `[prior]`
39. **Numeric drag-field sensitivity modifier** (Ctrl = slow, Shift = fast). `[prior]`
40. **"Look through this camera"** shortcut from non-active camera gizmos. `[both]`
41. **Lock affordance** (lock pick / transform / visibility) per entity. `[indep]`
42. **Sticky-pinned entities** beyond just Environment. `[prior]`
43. **Drag-drop reorder among siblings** in the hierarchy. `[prior]`
44. **Sky / atmosphere / fog editor** (currently only IBL HDR). `[indep]`
45. **Area / volume / IES light gizmos** (currently only Dir/Point/Spot). `[indep]`
46. **Measurement / ruler / scene-units overlay.** `[indep]`
47. **Bookmarks / saved cameras** ("save view 1", "F2 to recall"). `[indep]`
48. **Per-scene layout override** (some scenes want a wider Inspector). `[indep]`

### 4.4 Tier 4 — extensibility

49. **Editor → engine command bus** (typed). Panels reach through
    `ec.cameraController`, `ec.renderSystem` directly today (D1). `[prior]`
50. **Editor-internal event bus** (D4 fix). `[prior]`
51. **Play-mode toggle** with state snapshot/restore (D6 fix). `[prior]`
52. **Plugin / scripting hooks** — register custom panel, custom inspector
    section for a custom component, custom action. `[indep]`
53. **`ENABLE_EDITOR` build switch** — runtime can ship without editor. `[indep]`
54. **Live shader edit panel** (FileWatcher already exists in the engine). `[both]`
55. **Headless / scriptable editor mode** for automated tests. `[prior]`
56. **Localization framework** (strings via lookup). `[indep]`
57. **High-contrast theme variant + accessibility annotations.** `[indep]`
58. **Asset usage view** — "which entities reference this material?" `[prior]`
59. **Reimport / refresh** for textures and models. `[prior]`
60. **Folder view + import path bookmarks** instead of flat `assets/`. `[prior]`
61. **Asset hot-reload** from the asset browser context menu. `[prior]`
62. **Animation: import from glTF, retarget, blend trees.** `[indep]`
63. **Scene tagging / collections** (Unity-style layer/tag system). `[indep]`

---

## 5. Suggested next moves (ranked by leverage)

### 5.1 Quick wins — high signal, low risk

Same-day each:

1. **B1 + B2 + B7**: check `SceneSerializer::save()` return; restore
   selection on load failure; save-on-quit prompt.
2. **B8**: stop marking dirty on selection.
3. **B10**: wire `state.snapScale` into the scale drag.
4. **B22**: route F5 through the keybind path (or remove `toggleEditor`
   from the rebind UI and label it "hardcoded — engine restart key").
5. **B19**: revert the orphan `#include <algorithm>`.
6. **B20**: save/restore `GL_PACK_ALIGNMENT` and `GL_READBUFFER`; switch
   to back-buffer.
7. **B39**: drop the stale `#include "core/engine.h"`.
8. **B23**: clamp `leftW + rightW < workW - minCenter`.
9. **B25**: gate nav-gizmo hit on `viewportHovered`.
10. **Add missing `markSceneDirty()` calls** in `material_editor.cpp`,
    `bottom_panel.cpp`, `asset_browser.cpp`, `environment_inspector.cpp`
    (§2.2 list).

### 5.2 One-week projects — biggest leverage

These each dissolve a large chunk of the issue list:

- **Undo/Redo command stack** (Tier 1 #1) → makes §2.2 mostly
  obsolete; gives transactional grouping; recoverable gizmo drags.
- **Render-to-texture viewport target** (Tier 1 #8 / D3 / D20 / B20 /
  B21) → unlocks HDR-correct screenshots, ID-buffer picking,
  multi-viewport, and deletes the gizmo's projection remap.
- **Editor-internal event bus** (D4) → kills `hierarchyDirty`-style flags,
  unblocks D5 (cache invalidation by event), enables a proper Inspector
  refresh model, and frees panels from polling `EditorState`.
- **Add-Component / keybind registries** (D7) → adding a component or a
  binding becomes a single-file edit. Eliminates an entire class of
  "added a feature, forgot one of the four touchpoints" bugs.

### 5.3 Hygiene before this branch merges

- **Settings JSON versioning + atomic write** (B6 / Tier 1 #10).
- **Submodule cleanup** (D29). The editor refactor sitting on a feature
  branch of vkmGL with dirty inner submodules is a foot-gun for
  whoever merges this.
- **Editor tests scaffold** (D22 / Tier 1 #9) — even a thin one. The
  pieces most worth covering are pure: `EulerCache`, `AssetPicker` cap +
  error-code handling, `SceneIOController` paths, gizmo math
  (`computeScreenFactor`, `intersectRayPlane`,
  `distPointToSegment2D`), keybind label formatting.

---

## 6. Bottom line

The structural refactor is **done and good**. The framework layer (context /
state / settings / sceneIO / picker / shortcuts / resize) is well-shaped.
The remaining work splits into three buckets:

- **Correctness sweep** (§5.1) — ~1 day total, removes the silent-data-loss
  vectors, the lying keybind, and the GL state leaks.
- **Architecture** (§5.2) — four week-sized projects that each pay back a
  wide swath of the issue list. Undo + RTT viewport + event bus +
  registry is the "big four."
- **Capability** (§4) — the real "best-of" work. Without Tier 1 (undo,
  multi-select, prefabs, autosave, console, profiler) the editor is
  "modern" but not "long-term."

Two of the most surprising findings deserve singling out, because they're
the kind of thing that erodes user trust quickly:

- **B8** — clicking an entity in the viewport asks the user to save the
  scene. This makes the dirty marker useless: every interaction trips it.
- **B22** — the F5 keybind is rebindable in the Preferences UI but the
  rebind is silently ignored. A user who rebinds it and tries the new key
  has no path to discovery; the documented behavior is a lie.

Both are one-line fixes.
