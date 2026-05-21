# Editor Refactor — Audit & Wishlist

Snapshot of the editor on branch `vkm/dev/editor/refactor` after the
"full modernization pass" (`96e7d12`). Captures the current stage of the
refactor, a file-by-file issue inventory, and a wishlist of missing /
partial features.

**Date:** 2026-05-21
**Scope:** `src/editor/**`

---

## 1. Where the refactor stands

Three sequential phases have already landed:

- **Phase A — godfile decompose** (`2145759`, `51411bd`, `950489b`):
  the old monolithic `editor_system.cpp` (~550 LOC) was carved into
  [framework/scene_io_controller.cpp](../src/editor/framework/scene_io_controller.cpp),
  [framework/editor_menu_bar.cpp](../src/editor/framework/editor_menu_bar.cpp),
  [framework/editor_shortcuts.cpp](../src/editor/framework/editor_shortcuts.cpp),
  [framework/editor_status_bar.cpp](../src/editor/framework/editor_status_bar.cpp),
  [framework/editor_panel_resize.cpp](../src/editor/framework/editor_panel_resize.cpp).
  Now ~200 LOC shell at
  [editor_system.cpp](../src/editor/editor_system.cpp).
- **Phase B — Engine::get() purge** (`ed41537`, `726b870`): scene IO and
  the panels no longer reach into a global singleton; everything goes
  through `EditorContext& ec`.
- **Phase C — "full modernization pass"** (`96e7d12`, +8147 lines):
  EditorPanel base class deleted, panels became POD draw-objects,
  gizmos split 3-way (`_hit` / `_draw` / `_drag`), five duplicated file
  pickers merged into
  [framework/asset_picker.cpp](../src/editor/framework/asset_picker.cpp),
  settings persistence
  ([framework/editor_settings.cpp](../src/editor/framework/editor_settings.cpp)),
  dirty tracking, MRU, screenshot, light/camera viewport gizmos,
  animation timeline in
  [panels/bottom_panel.cpp](../src/editor/panels/bottom_panel.cpp),
  Frame-All action, F5 hint overlay.

**Working tree at audit time:** only one in-flight change —
[transform_gizmo_draw.cpp](../src/editor/gizmo/transform_gizmo_draw.cpp)
gained an unused `#include <algorithm>`. Aborted mid-edit; safe to
revert or finish.

**Net shape now** (60 files, ~8.1k lines):

```
src/editor/
├── editor_system.{h,cpp}       shell (init/teardown + drawWorkspace)
├── framework/                  state · context · settings · sceneIO ·
│                               menubar/statusbar/shortcuts/resize ·
│                               asset_picker · screenshot
├── panels/                     hierarchy · inspector · bottom · material ·
│                               prefs · asset_browser · environment_inspector
├── overlays/                   viewport_overlay (stats+nav) · gizmo_overlay ·
│                               viewport_toolbar · playback_bar
├── gizmo/                      transform_gizmo + _hit/_draw/_drag
├── input/                      editor_actions · editor_keybinds
└── ui/                         editor_theme · editor_style · editor_icons ·
                                editor_widgets
```

The structural refactor is essentially **done**. What's left is
correctness, polish, and a missing command/undo layer.

---

## 2. Verified high-confidence issues

### 2.1 Silent save / load failures (data-loss vector)

- [scene_io_controller.cpp:41](../src/editor/framework/scene_io_controller.cpp#L41)
  `SceneSerializer::save()` return value is ignored; `state.sceneDirty
  = false` runs unconditionally. A failed save (disk full, permission,
  locked file) looks like success.
- [scene_io_controller.cpp:70-72](../src/editor/framework/scene_io_controller.cpp#L70-L72)
  Load fail path: selection is cleared above and never restored on
  failure. Typing a bad path silently drops your selection.
- [scene_io_controller.cpp:61](../src/editor/framework/scene_io_controller.cpp#L61)
  Hand-rolled `APP_ROOT_DIR + "/scenes/scene.json"`; no
  `create_directories()` for the scenes dir.
- No autosave, no `.bak` rotation, no overwrite-protection if the file
  changed on disk.

### 2.2 `markSceneDirty()` is missing across whole panels

Grep tally — 23 calls across 6 files. Three panels and one core
overlay don't call it **at all**:

| File | `markSceneDirty()` calls | Verdict |
|---|---|---|
| [panels/material_editor.cpp](../src/editor/panels/material_editor.cpp) | **0** | every PBR edit is silent |
| [panels/bottom_panel.cpp](../src/editor/panels/bottom_panel.cpp) | **0** | animation keyframe edits silent |
| [panels/asset_browser.cpp](../src/editor/panels/asset_browser.cpp) | **0** | material/mesh assign silent |
| [panels/environment_inspector.cpp](../src/editor/panels/environment_inspector.cpp) | 2 | most fields silent |
| [panels/inspector_panel.cpp](../src/editor/panels/inspector_panel.cpp) | 12 | best coverage |
| [panels/hierarchy_panel.cpp](../src/editor/panels/hierarchy_panel.cpp) | 2 | partial |
| [overlays/gizmo_overlay.cpp](../src/editor/overlays/gizmo_overlay.cpp) | 2 | gizmo drags need a re-audit |
| [input/editor_actions.cpp](../src/editor/input/editor_actions.cpp) | 4 | entity ops covered |

Spot-confirmed silent mutations:

- [inspector_panel.cpp:95](../src/editor/panels/inspector_panel.cpp#L95)
  — adds a Name component without marking dirty.
- [asset_browser.cpp:146](../src/editor/panels/asset_browser.cpp#L146)
  — material assignment via context menu.
- [asset_browser.cpp:198](../src/editor/panels/asset_browser.cpp#L198)
  — mesh assignment via context menu.

The Phase C commit message claims "every mutator calls
`state.markSceneDirty()`" — by a wide margin, it doesn't.

### 2.3 GL state leaks in screenshot capture

- [screenshot.cpp:45-46](../src/editor/framework/screenshot.cpp#L45-L46)
  Sets `GL_PACK_ALIGNMENT` and `GL_READBUFFER` without restoring them.
- [screenshot.cpp:46](../src/editor/framework/screenshot.cpp#L46)
  `glReadBuffer(GL_FRONT)` is driver-racy under a compositor; canonical
  capture target is the last bound FBO or `GL_BACK` before swap.

### 2.4 Gizmo: scale snap never reads `state.snapScale`

- [transform_gizmo_drag.cpp:75](../src/editor/gizmo/transform_gizmo_drag.cpp#L75)
  Rotation drag respects `m_snapAngle` (correct, delta-based — earlier
  drift suspicion was wrong); scale drag never references the snap
  field. `EditorState::snapScale` exists; the gizmo never consumes it.
- [transform_gizmo_drag.cpp:82](../src/editor/gizmo/transform_gizmo_drag.cpp#L82)
  `glm::decompose` discards `skew`; sheared models silently lose shear
  after a scale drag.

### 2.5 Gizmo: orthographic camera path is broken

- `m_screenFactor = GIZMO_SIZE_PIXELS / ndcLength` in
  [transform_gizmo.cpp](../src/editor/gizmo/transform_gizmo.cpp)
  assumes a perspective projection. Ortho cameras yield a gizmo that
  scales with object distance rather than staying constant on screen.
- `AXIS_HIT_RADIUS = 10.0f` is not DPI-aware.

### 2.6 Light gizmo picking ignores the light's real volume

- [gizmo_overlay.cpp](../src/editor/overlays/gizmo_overlay.cpp): light
  picking sphere is hardcoded to 0.5 world units. Large point lights
  become unpickable by their gizmo; tiny lights become oversized hit
  targets.
- `forEach<Light, Transform>` scans every light every frame with no
  frustum cull and no skip on disabled lights.

---

## 3. Smells and second-tier issues (by file)

### Framework

- [editor_settings.cpp](../src/editor/framework/editor_settings.cpp) —
  no version key on the JSON (first incompatible rename wipes
  bindings); writes directly over the file (no temp+rename atomicity);
  catches all JSON exceptions silently.
- [editor_menu_bar.cpp](../src/editor/framework/editor_menu_bar.cpp) —
  string-search path parsing instead of `std::filesystem::path`. Minor.
- [editor_status_bar.cpp](../src/editor/framework/editor_status_bar.cpp)
  — hierarchy walk has no cycle guard (relies on engine-side
  guarantee); `char chain[192]` truncates ancestor chains with no
  feedback.
- [editor_shortcuts.cpp](../src/editor/framework/editor_shortcuts.cpp)
  — clean.
- [editor_panel_resize.cpp](../src/editor/framework/editor_panel_resize.cpp)
  — drag flags survive F5 hide; grabbing a splitter then hiding causes
  ghost-drag on re-show.
- [asset_picker.cpp](../src/editor/framework/asset_picker.cpp) —
  `std::error_code` passed to `directory_iterator` but never checked;
  "first 4000 entries" cap is silent.

### Panels

- [inspector_panel.cpp](../src/editor/panels/inspector_panel.cpp) —
  hardcoded `if (scene.has<X>)` dispatch at lines 114-119 means every
  new component type requires editing the inspector. No multi-select.
  No copy/paste components. No locked inspector.
- [hierarchy_panel.cpp](../src/editor/panels/hierarchy_panel.cpp) —
  O(N) filter rebuild on every keystroke; drag-drop reparent has no
  visible reason-failure; no keyboard nav, no rename-in-place.
- [bottom_panel.cpp](../src/editor/panels/bottom_panel.cpp) —
  `drawAnimationSection()` is ~320 LOC (longest function in the
  codebase); linear scan through all keyframe times per mouse frame;
  preview pose writes back into the live `Transform` without dirty
  flag; magic layout literals (laneH 16, rulerH 18, ticks 10, gap 8).
- [material_editor.cpp](../src/editor/panels/material_editor.cpp) —
  `m_pendingTexture` is a raw pointer into an asset, dangles if the
  asset reloads mid-modal; live preview re-renders every frame
  (`live=true`); hardcoded texture extension lists in multiple places.
- [asset_browser.cpp](../src/editor/panels/asset_browser.cpp) — no
  pagination; full grid widget creation every frame; no drag-drop
  assignment; tooltips show only the name.
- [environment_inspector.cpp](../src/editor/panels/environment_inspector.cpp)
  — bloom-preset detection uses float `< 5e-4f` against hardcoded
  preset values; `m_iblPathMemo` etc. have no invalidation hook on
  external mutation; hardcoded `SetNextItemWidth(-150.0f)` ignores
  `EditorStyle::LABEL_WIDTH`.
- [preferences_panel.cpp](../src/editor/panels/preferences_panel.cpp) —
  `m_fpsLimitEdit` not re-synced on each open; pointer-identity
  keybind-row tracking (`m_rebindTarget == labelLiteral`) silently
  breaks if labels become `std::string`.

### Overlays + gizmos

- [gizmo_overlay.cpp](../src/editor/overlays/gizmo_overlay.cpp) —
  viewport-pos remap math needs a hover-pick test under non-zero
  `viewportPos.y`; light gizmo position falls back to local-space
  `tf.position` when no `WorldTransform`, which is wrong for parented
  lights.
- [transform_gizmo.cpp](../src/editor/gizmo/transform_gizmo.cpp) —
  behind-camera cull only checks origin's `clip.w`; arrow head, center
  dot, ring alpha clamp colors and pixel sizes hardcoded outside
  `EditorStyle`.
- [transform_gizmo_hit.cpp](../src/editor/gizmo/transform_gizmo_hit.cpp)
  — rotation ring hit-test projects twice per frame; plane quad hit
  test is fragile at near-parallel screen-space axes.
- [transform_gizmo_draw.cpp](../src/editor/gizmo/transform_gizmo_draw.cpp)
  — uncommitted `#include <algorithm>` is unused (abandoned mid-edit).
- [viewport_overlay.cpp](../src/editor/overlays/viewport_overlay.cpp)
  — hardcoded overlay width 276px; nav-gizmo positioning assumes
  positive viewport extents.
- [viewport_toolbar.cpp](../src/editor/overlays/viewport_toolbar.cpp)
  / [playback_bar.cpp](../src/editor/overlays/playback_bar.cpp) —
  hover state lost if the child window fails to open (no hysteresis);
  position math underflows on very-small viewports.

### UI primitives

- [editor_style.h](../src/editor/ui/editor_style.h) — token surface
  too thin (`LABEL_WIDTH`, `TITLE_SCALE` only); button sizes 26/32/56,
  card indent 14, pad (8,7), spacing 6/9/14/18 scattered as literals.
- No DPI scaling pipeline anywhere; `26.0f` is a constant, not
  `26.0f * dpiScale`.
- [editor_widgets.cpp](../src/editor/ui/editor_widgets.cpp) —
  `thread_local cardStack()` is fine for single-threaded ImGui but
  nothing asserts balanced begin/end; `EulerCache<Key>` has no
  gimbal-lock fallback at pitch ≈ ±90°; `drawRemoveButton` assumes
  `SmallButton` is exactly 20px wide.
- [editor_icons.cpp](../src/editor/ui/editor_icons.cpp) — re-rasterized
  every frame (no atlas); inconsistent pixel-snapping across icons;
  missing folder / file / gear / warning / eye / color-swatch /
  drag-grip glyphs.
- [editor_theme.cpp](../src/editor/ui/editor_theme.cpp) — no runtime
  theme switch / hot reload; `DragDropTarget` color is a literal.

### Input

- [editor_keybinds.cpp](../src/editor/input/editor_keybinds.cpp) —
  `strcat` into a 32-byte buffer in `keybindLabel`; safe in practice
  but unchecked. No keybind conflict detection.
- [editor_actions.cpp](../src/editor/input/editor_actions.cpp) — model
  import failure path doesn't surface error to the user.

---

## 4. Architectural smells

- **`EditorContext` is a fat aggregate** that grows linearly with
  engine systems. Today: `cameraController`, `renderSystem`,
  `visibilitySystem`, `events`, `viewportPos/Size`. Adding any new
  system (physics, audio, navmesh) means editing the struct *and*
  every panel. A typed registry (`ec.get<RenderSystem>()`) or
  per-panel injection would scale better.
- **No editor-internal event bus.** Selection change, scene loaded,
  entity created, gizmo drag start/end — none of these are broadcast.
  Panels poll `state.selectedEntity` and infer; that's why every panel
  needs `state.hierarchyDirty`-style ad-hoc flags.
- **No edit-mode vs. play-mode distinction.** Animation preview writes
  back into the live `Transform`
  ([bottom_panel.cpp:58-60](../src/editor/panels/bottom_panel.cpp#L58-L60)).
  No "exit play mode reverts changes" model.
- **`EnvironmentConfig` singleton coupling.** Several panels reach for
  the Environment entity directly. It's a singleton in disguise; if
  you ever want per-camera or per-scene environments, every consumer
  changes.
- **No render-to-texture viewport.** The viewport area is a "hole" in
  ImGui with the main framebuffer behind it. That's why screenshot
  uses `glReadBuffer(GL_FRONT)`. A proper offscreen FBO would unlock:
  viewport thumbnails, multi-viewport, HDR-correct screenshots,
  picking via ID buffer (cheaper than ray-vs-AABB).
- **Resource handles held as raw pointers in modal state.** Material
  editor `m_pendingTexture` is the obvious one; the pattern repeats.
  Generational handles would survive resource reloads.
- **No editor tests.** `tests/editor/` doesn't exist. 8k LOC of new
  code has no regression net. Selection state, dirty tracking,
  settings round-trip, gizmo math, keybind dispatch — all live-tested
  only.
- **Zero TODO/FIXME/HACK comments** across `src/editor/`. Either
  suspiciously polished or all TODOs were scrubbed before commit.

---

## 5. Submodule state

This branch sits on top of dirty nested submodules:

```
modules/vkmGL  → 69125da... (heads/major-update-dev)   detached-ish, feature branch
  └─ modules/glew    M   (uncommitted)
  └─ modules/glm     M   (uncommitted)
  └─ modules/vkmLog  M   (uncommitted)
```

The editor refactor isn't a `master`-tracked branch in vkmGL. Easy to
ship the editor refactor with an unintended GL/glm/vkmLog bump.

---

## 6. Wishlist — what's missing or partial

Grouped by impact. Bold = highest leverage.

### 6.1 Critical foundations

1. **Undo / Redo (Command pattern).** No inspector edit, no gizmo
   drag, no entity op is undoable. Single largest missing feature —
   once it exists, the dirty-tracking gaps become irrelevant (commands
   flip dirty automatically), bulk operations get transactional
   grouping, and asset-browser / material-editor edits become
   recoverable.
2. **Atomic save + autosave + `.bak` rotation.** A crashed save
   corrupts the scene file silently. Need temp-write + rename,
   periodic autosave (1-5 min), and crash-recovery on next launch.
3. **Save/load failure surfacing.** A user-visible toast/log when save
   or load fails.
4. **Multi-select.** `EditorState::selectedEntity` is a single
   `EntityId`. Promote to `std::vector<EntityId>` (or a `SelectionSet`
   aggregate); update Inspector/Hierarchy/gizmo accordingly.
5. **Settings JSON versioning + atomic write.**

### 6.2 Inspector / editing UX

6. **Copy / paste components** + "paste values" on a single field via
   right-click-on-label context menu (Unity/Unreal idiom).
7. **Locked inspector** (pin the current entity).
8. **Drag-drop assignment** from Asset Browser to Inspector fields and
   mesh slots directly.
9. **Component reordering** inside an entity.
10. **Add-Component registry.** Replace hardcoded
    `if (scene.has<X>)` dispatch with a component-type registry
    (`name() / category() / draw()` per type).
11. **Per-field Revert / Reset-to-default.**
12. **Searchable enum dropdowns** (long enums like RenderMode benefit
    immediately).

### 6.3 Hierarchy / Scene

13. **Inline rename** (F2 / double-click), with collision validation.
14. **Keyboard nav** (up/down/left/right tree walk, Enter rename,
    Delete delete, Ctrl+D duplicate).
15. **Search ranking** with weighted match (exact > prefix >
    substring).
16. **Sticky-pinned entities** beyond just Environment.
17. **Drag-drop reorder among siblings** (not just reparent), with a
    visible drop indicator.

### 6.4 Gizmos

18. **Scale snap** (field exists in EditorState; gizmo never reads
    it).
19. **Orthographic-camera support** for `m_screenFactor`.
20. **DPI-aware hit radius.**
21. **Plane handles for scale**, **screen-space (uniform) handle**,
    **angle readout while rotating**, **distance readout while
    translating**.
22. **Marquee (rubber-band) selection** in the viewport.
23. **Snap-to-vertex / snap-to-grid / snap-to-face** for translation.
24. **Click-through** to pick the entity behind the current selection.

### 6.5 Assets

25. **Pagination / virtualization** in the Asset Browser grid.
26. **Drag-drop** from Browser to viewport / inspector / hierarchy.
27. **Asset usage view** — "which entities reference this material?"
28. **Reimport / refresh** for textures and models.
29. **Folder view + import path bookmarks** instead of flat `assets/`.
30. **Asset hot-reload** triggered from the asset browser context
    menu.

### 6.6 Viewport / Rendering

31. **Render-to-texture viewport target** (unlocks proper screenshots,
    thumbnails, ID-buffer picking).
32. **Picking respects gizmo z-order** (verified pickable through
    toolbar hover — needs hover-race recheck).
33. **Light gizmo culling**: skip disabled lights, frustum-cull
    off-screen lights.
34. **Light gizmo picking radius from light volume** (not hardcoded
    0.5).
35. **"Look through this camera" shortcut** from non-active camera
    gizmos.
36. **HDR / linear-sRGB toggle on screenshots** + capture-to-FBO.

### 6.7 Bottom panel

37. **Console panel** — engine log viewer with severity filter +
    search. Currently the bottom panel only has Animation +
    Statistics; engine log output is invisible from the editor.
38. **Profiler tab** — frame time breakdown by system + GPU pass.
39. **Output/build-log dock** that tails compiler errors when shaders
    or scripts hot-reload.

### 6.8 Animation panel

40. **Spatial structure for keyframes** (current linear scan).
41. **Curve editor** alongside dope-sheet.
42. **Animation preview vs. authoring distinction** — currently the
    preview pose can leak into the scene without marking it dirty.

### 6.9 Theming / accessibility

43. **DPI scale pipeline** — single multiplier applied to all
    `EditorStyle::*` and icon sizes.
44. **Theme tokens for spacing/padding/button sizes** — kill the
    literals.
45. **Markdown / rich tooltips.**
46. **Keyboard navigation across custom widgets** (icon rows,
    hierarchy, asset grid).
47. **Theme hot-reload** (or at minimum a `Reload Theme` menu item).

### 6.10 Input

48. **Keybind conflict detection** in Preferences.
49. **Per-context keybind layers** (viewport vs. inspector vs.
    hierarchy).
50. **Mouse-button bindings**, not just keys.
51. **Numeric drag-field sensitivity modifier** (Ctrl = slow,
    Shift = fast — Unity convention).

### 6.11 Engine ↔ editor seam

52. **Editor → engine command bus.** Panels reach through
    `ec.cameraController`, `ec.renderSystem`, etc. directly. A typed
    command bus (or an extension of the existing event system) would
    let new panels appear without growing `EditorContext`.
53. **Editor-internal event bus** — `OnSelectionChanged`,
    `OnSceneSwapped`, `OnEntityCreated`, etc.
54. **Play-mode toggle** with state snapshot/restore (the playback bar
    exists; the semantic doesn't).
55. **Prefab system** (Save Entity as Prefab + drag-instance).
56. **Per-panel dock-state save** beyond the global `imgui.ini` (named
    layouts: "Animator", "Lighting", "Default").
57. **Live shader edit** with reload-on-save.
58. **Headless / scriptable editor mode** for tests — currently the
    editor is all-or-nothing.
59. **Editor tests** — at minimum, settings round-trip, selection
    lifetime through scene load, gizmo math, dirty-flag coverage
    matrix.

---

## 7. Suggested next moves

- **Quick wins (low risk, high signal)**:
  - Restore selection on load failure
    ([scene_io_controller.cpp:70-72](../src/editor/framework/scene_io_controller.cpp#L70-L72)).
  - Check `SceneSerializer::save()` return value before clearing
    dirty.
  - Add the missing `markSceneDirty()` calls in `material_editor.cpp`,
    `bottom_panel.cpp`, `asset_browser.cpp`,
    `environment_inspector.cpp`.
  - Save+restore `GL_PACK_ALIGNMENT` / `GL_READBUFFER` in
    [screenshot.cpp](../src/editor/framework/screenshot.cpp); use the
    back buffer instead of front.
  - Revert the orphan `#include <algorithm>` in
    [transform_gizmo_draw.cpp](../src/editor/gizmo/transform_gizmo_draw.cpp).
- **One-week project**: Undo/Redo command system. Dissolves a quarter
  of the issues above (dirty tracking, transactional asset edits,
  recoverable gizmo drags).
- **One-week project**: Render-to-texture viewport target. Unlocks
  HDR-correct screenshots, ID-buffer picking, multi-viewport.
- **Hygiene**: editor tests scaffold, settings versioning + atomic
  write, vkmGL submodule cleanup before this branch merges.
