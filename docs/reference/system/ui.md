# UI System

In-game, screen-space UI modelled as ECS. A UI element is an entity with
plain-struct components; the `UISystem` resolves 2D layout, hit-tests the
pointer, and builds a batched draw list that rides the **existing `RenderView`
seam** into the backend, where one `GLUIPass` draws it on top after Composite.
It runs identically in `engine_runtime` and `engine_editor` and pulls in **no
ImGui** - ImGui stays the editor's own tooling, never the shipped game UI.

> The one idea: **UI is just more ECS.** Because a UI element is an entity,
> it inherits the entity hierarchy (nesting), the editor's inspector / selection
> / undo, and scene serialization with almost no UI-specific machinery.

## Key files

- `src/engine/ecs/component/ui_canvas.h` - `UICanvas` (layer root + reference-resolution scaling + sort order)
- `src/engine/ecs/component/ui_element.h` - `UIRect` and `UIElement` (the 2D rect: anchor / pivot / position / size / visible)
- `src/engine/ecs/component/ui_image.h` - `UIImage` (tinted quad)
- `src/engine/ecs/component/ui_text.h` - `UIText` (string + font name + size + colour + h/v alignment)
- `src/engine/ecs/component/ui_button.h` - `UIButton` (per-state tints + event id)
- `src/engine/system/ui/ui_system.{h,cpp}` - `UISystem` (layout resolve + draw build + interaction resolve)
- `src/engine/system/ui/ui_draw_data.h` - `UIVertex` / `UIDrawCmd` / `UIDrawData` (the engine -> backend contract)
- `src/engine/system/ui/ui_events.h` - `UIClickEvent`
- `src/engine/resource/asset/font_asset.h` - `FontAsset` (self-contained SDF atlas pixels + per-glyph metrics)
- `src/tools/font/font_baker.{h,cpp}` - `bakeFontSDF` (stb_truetype -> SDF atlas)
- `src/backend/opengl/pass/gl_ui_pass.{h,cpp}` - `GLUIPass` (the 2D overlay pass)
- `shaders/ui/` - the UI shader (Solid / SDF-text in one program)

## The component model

| Component | Holds | Notes |
|-----------|-------|-------|
| `UICanvas` | reference height, scale mode, sort order, visible | Root of a UI layer; spans the viewport. A scene can have several (HUD, menu); they draw - and hit-test - in ascending `sortOrder`. Put it on a root entity and parent `UIElement`s under it. |
| `UIElement` | anchor, pivot, position, size, visible (+ resolved `screenRect`) | The 2D analogue of `Transform`. Nesting reuses the **normal entity hierarchy**; `visible = false` skips the element and its whole subtree. |
| `UIImage` | colour | A filled (and later textured) quad over the element's rect. |
| `UIText` | text, font name, pixel size, colour, align, valign | SDF text, aligned in the rect on both axes; the font is named, not handle-referenced (see Text). |
| `UIButton` | normal/hover/pressed/disabled tints, eventId, interactable | Hit-tested topmost-wins; the `UISystem` drives its visual `state` and fires `UIClickEvent`. |

`screenRect` (on `UIElement`, a `UIRect`) and `state` (on `UIButton`) are
resolved every frame, so they are **not** reflected and do not serialize.

## Per-frame flow

`UISystem` runs in the **Transform stage, right after `HierarchySystem`** - UI
layout is a screen-space transform resolve, the 2D sibling of resolving
`WorldTransform`. It runs in both binaries (wired in `setupEngineApp`).

```
UISystem::update(FrameContext)
  |-- read pointer (viewport-local) + this frame's press/release edges
  |-- collect visible UICanvases, sort by sortOrder (entity index breaks ties)
  |-- for each canvas, in order:
  |     derive a uniform scale from the viewport (ScaleWithHeight)
  |     walk its hierarchy parent-before-child (skipping invisible subtrees):
  |       resolve UIElement.screenRect from anchor/pivot/position/size x scale
  |       emit a quad per UIImage
  |       emit a quad per UIButton + record it as a hit candidate
  |       lay out glyph quads per UIText (font resolved by name)
  |-- resolveInteraction(): topmost candidate wins the pointer; states +
  |     button quad colours settled; UIClickEvent fired on click
  |-- publish the batched UIDrawData on ctx.ui   (mirrors ctx.visibility)

RenderSystem::update
  |-- RenderView::build copies ctx.ui into view.ui (camera-independent: it
  |   survives the no-camera path, so a HUD/menu draws with nothing 3D in view)
  |-- backend.render(view) -> ... -> Composite -> GLUIPass (last pass)
```

The hand-off is the same idiom the `VisibilitySystem` uses: the UISystem owns
the buffer and points `ctx.ui` at it; `RenderView` snapshots it. No system
reaches into another.

## Layout: anchor / pivot

`anchor` and `pivot` are normalised `0..1` with a top-left origin. `anchor`
picks the point of the parent rect to pin to; `pivot` picks the point of the
element that lands there. `position` and `size` are authored in the canvas's
reference pixels and multiplied by the canvas scale, so an element stays fixed
to a corner / edge / centre and keeps its proportions across resolutions.
`UIRect` (pos + size, top-left origin screen pixels) is the unit the whole
pass works in: parent rects seed child resolution and hit-testing is
`rect.contains(pointer)`.

```
screenRect.pos = parentPos + anchor*parentSize + position*scale - pivot*(size*scale)
```

Top-left HUD element: `anchor = pivot = (0,0)`. Screen-centred: `(0.5,0.5)` -
a button label is just a child element with centre anchor/pivot and `Middle`
valign, no hand-tuned offsets. Stretch-to-fill anchors, layout containers, and
9-slice are deferred.

## Text: SDF fonts

`FontAsset` is a `ResourceManager` asset holding the single-channel
signed-distance atlas **as pixels**, plus per-glyph metrics (and
ascent/descent/lineHeight) for printable ASCII. `bakeFontSDF` (in
`vkm_tools`) renders each glyph with `stbtt_GetCodepointSDF` and packs them
with `stb_rect_pack` (warning if any glyph does not fit). Because the atlas
stores distance, **one bake stays crisp at any size**; the UISystem just scales
the metrics by `requestedSize / bakedHeight`, and the shader anti-aliases the
edge with a screen-space derivative. `valign` places the ascent..descent block
Top / Middle / Bottom within the element rect.

The asset is deliberately **self-contained** - no handle into the texture slot.
Fonts are runtime-baked and never enter scene files, so `SceneSerializer::load`
swaps the `FontAsset` slot back across the asset-graph swap exactly like the
`ShaderAsset` slot; self-containment is what makes that safe. The consequence:
**fonts survive scene load with no re-bake**, and `ensureDefaultUIFont` (in
`app/engine_app.h`, baking `"ui:roboto"` from `assets/fonts/`) runs once at
startup only.

`UIText` references its font by **asset name**, not a handle: names are the
serializable asset identity, so the component stays plain data, and the
per-frame `findByName` is O(1).

## Interaction

The UISystem hit-tests the pointer (in viewport-local pixels) against each
interactable `UIButton`'s resolved rect during the walk, but only **records
candidates**; once the frame's draw list is complete, `resolveInteraction()`
picks the **topmost** candidate under the pointer (the last in painter order,
across canvases in sort order). Only that button hovers or presses -
overlapping buttons never light up together. A press starts a click candidate
on the topmost button; releasing over that same button **enqueues a
`UIClickEvent`** through the frame's `EventBus` (`ctx.events`). Gameplay reacts the same way it would to any event:

```cpp
subscribe<UIClickEvent>([this](const UIClickEvent& e) {
    if (e.eventId == "play") startGame();
});
```

In the editor the pointer is shared with the editor's own chrome, so
`EditorSystem` calls `UISystem::setEditorPointerCapture()` each frame with the
same flag it hands the camera controller. While it is set the layout still runs
and the overlay still draws, but nothing hit-tests - a click aimed at the
viewport toolbar, the playbar or a gizmo does not also press the game button
behind it. The runtime never sets it.

## The draw seam

`UIDrawData` is a backend-agnostic POD: a flat `UIVertex` stream (screen-pixel
position, uv, straight RGBA) plus `UIDrawCmd`s (a vertex range + a kind; Text
commands carry the `FontHandle` whose atlas they sample). `RenderView` carries
it exactly like the 3D snapshot, which keeps backends interchangeable - the GL
backend is the only consumer today, but nothing in the frontend is GL-specific.

## Backend: GLUIPass

`GLUIPass` is appended **after Composite** (pass #11). It binds the same
backbuffer rect, streams `UIDrawData` into a dynamic vertex buffer, and draws
each command under an orthographic projection - alpha-blended, depth off.
`GLView::sync` uploads font atlases into their own table (keyed by
`FontHandle` - fonts are not `TextureAsset`s), and the pass resolves them per
Text command. The `shaders/ui` program branches per command: Solid (flat
tint) or Text (SDF coverage). It is a no-op when the draw list is empty, so
non-UI scenes pay nothing.

## Serialization

Each UI component's authored fields are reflected (`VKM_REFLECT`, with
`VKM_ENUM_NAMES` for `ScaleMode`/`Align`/`VAlign`) and round-trip as reflected
passthroughs in `ComponentSerializer` / `SceneSerializer`, registered in
`COMPONENT_KEYS`. UI entities save and load with the scene like any other.

## Editor authoring

Because UI elements are entities, the editor support is mostly inherited:

- **Hierarchy** lists entities by `Transform` **or** `UICanvas`/`UIElement`, so
  UI entities (which carry no `Transform`) appear in the tree.
- **Inspector** has a card per UI component (`drawUI*Section`), built from the
  shared `prop*` widgets; Add / Remove / field-edit all route through the
  command stack, so authoring is fully undoable.
- **Add Component** menu has a "UI" group; the **Create** menu has a "UI"
  submenu (Canvas / Panel / Text / Button) that adds a `UIElement`/`UICanvas`
  instead of a `Transform` and parents a new element under the selected
  canvas/element.

A visual on-canvas editor (rect handles) is deferred; authoring is done through
the inspector for now.

## Deliberately deferred

Layout containers (rows / stacks), 9-slice sprites, sprite textures on
`UIImage` (which re-adds an Image draw kind), opaque images blocking the
pointer, an auto-created label child for editor-made buttons (needs a
multi-entity create command), multi-line / word-wrapped text, UI animation,
world-space / diegetic UI, and a visual 2D edit mode. None require reworking
the above - they are additive components or passes.
