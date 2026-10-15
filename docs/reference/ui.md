# UI System

In-game, screen-space UI modelled as ECS. A UI element is an entity with
plain-struct components; `UISystem` lays them out in 2D, hit-tests the pointer,
and builds a batched draw list that `GLUIPass` draws on top after Composite. It
runs in every host that runs `setupEngineApp` and uses **no ImGui** - ImGui is
the editor's own tooling. Being entities, UI elements get nesting, the editor's
inspector and undo, and scene serialization for free.

## The component model

Each header in `ecs/component/ui/` documents its fields.

| Component | Notes |
|-----------|-------|
| `UICanvas` | Root of a UI layer; spans the viewport. Several canvases draw - and hit-test - in ascending `sortOrder`. |
| `UIElement` | The 2D analogue of `Transform`. `visible = false` skips the whole subtree; `opacity` multiplies down it. |
| `UIImage` | A filled quad over the rect. `shape` (a `UIShape`) adds rounded corners, a border and a gradient; a `texture` stretches a picture over the rect, tinted. |
| `UIText` | SDF text aligned in the rect. A `shadow` colour draws it again underneath, for text over the scene with no panel. |
| `UIButton` | Fires `UIClickEvent`; also the drag primitive. See Interaction. |
| `UIScroll` | Makes the element a window onto its children. Always clips. |
| `UISlider` | Rides a `UIButton`, its track. |
| `UIToggle` | Rides a `UIButton` and flips on its click. |
| `UIChoice` | Steps through its options on two `UIButton`s' clicks. |

Resolved fields are recomputed every frame and do not serialize.

**Two structural rules**, both of which leave a full card and an empty
viewport ([the editor names them](editor.md#a-card-names-what-its-component-is-waiting-for)):
an element with no `UICanvas` ancestor is never visited - strictly an ancestor,
so a `UIElement` on the canvas entity itself is never resolved
(`hasCanvasAncestor`, `ecs/component/ui/ui_canvas.h`); and a `UIImage` /
`UIText` / `UIButton` / `UIChoice` on an entity with no `UIElement` is never reached.

## Layout: anchor / pivot

`anchor` and `pivot` are normalised `0..1` from the top left. `anchor` picks the
point of the parent rect to pin to; `pivot` the point of the element that lands
there. `position` and `size` are in the canvas's reference pixels, scaled with
the canvas, so an element keeps its corner and proportions at any resolution.

```
sizePx         = max(size*scale + relativeSize*parentSize, 0)
screenRect.pos = parentPos + anchor*parentSize + position*scale - pivot*sizePx
```

`relativeSize` is the share of the parent an element adds to `size`: `(1, 1)`
with zero `size` is a full-screen scrim, `(1, 0)` with a height a full-width
bar, `(1, 1)` with a negative `size` the parent less a margin.

`UIElement::at(anchorPivot, position, size)` uses one point as both anchor and
pivot: `(0,0)` top left, `(0.5,0.5)` centred.

## Clipping and scrolling

`UIElement::clipChildren` bounds what descendants draw to the element's rect,
intersected with every clipping ancestor. It bounds the **pointer** too: a
button clipped out of sight is not hit and does not block.

`UIScroll` lays its children out against its rect shifted by `offset`, and
always clips; parent the content under it as usual. **It measures its own
content**: `contentSize` is how far what the descendants drew reaches (a
subtree that itself clips reports only its rect). `offset` is clamped to
`range()` (`contentSize - viewSize`, never negative) every frame, so writing a
large number means "the bottom", and `offset / range()` is the fraction a
project's own scrollbar needs.

The wheel scrolls the innermost scroll view under the pointer that no other
blocker covers, else the one holding the topmost blocker, on whichever axis
overflows, vertical first. Over a blocking element `input().wheel()` reads zero,
so scrolling a list is never also a gameplay action.

## Text

`UIText::text` is UTF-8, drawn from a `FontAsset`: a signed-distance atlas, so
one bake serves a wide range of `pixelSize`. The font is referenced by **asset
name**, not a handle; a name nothing answers to draws nothing, and the
inspector's UI Text card shows it in red.

A bake covers printable ASCII, U+00A0-U+00FF and `FontAsset::MARKS`. An uncovered character draws and advances
nothing; a malformed byte is skipped.

A newline always breaks; `wrap` also breaks at the last space that fits the
width, or mid-word for a word wider than the rect. `valign` places the whole
block, which is not bounded to the rect - clip above it if it may overflow.
`walkGlyphs` (`system/ui/text_layout.h`) is the one line walk; wrapping, quads
and `measureText` all use it.

**A button's label rides the button's own entity**: image, button and text draw
from one rect in that order, so a `UIText` with `align = Center` and
`valign = Middle` on the button is its label - no child needed.

## Interaction

The topmost element under the pointer that **blocks** wins it: one that draws (a
`UIImage` or `UIButton`) with `UIElement::blocksPointer` on, the last in painter
order across canvases in sort order. So an opaque panel over the HUD takes the
click from the buttons beneath. Turn `blocksPointer` off for overlays meant to
be clicked through (vignette, crosshair). Text never blocks.

Only the topmost blocker hovers or presses. A press and release over the same
button **enqueues a `UIClickEvent`** on the frame's `EventBus`:

```cpp
subscribe([this](const UIClickEvent& e) {
    if (e.eventId == "play") startGame();
});
```

A press of `UI/Click`'s mouse button that begins over a blocker belongs to the
UI: it reads as down for `UI/Click` and up for every other action until
released, so a click on a menu never fires a weapon and gameplay code never has
to ask.
`input().pointerOverUI()` answers the same question for anything else, such as
hiding a crosshair. With the cursor disabled for mouse-look
(`CursorMode::Disabled`) nothing is hit-tested. In the editor, while the
editor's chrome holds the pointer (`ctx.chrome`), nothing hovers, presses or
scrolls; the UI still lays out, draws and resolves `UIDrawData::pointerTarget`,
the topmost blocker the editor's picker reads.

**Dragging.** A button's `held` stays true from a press on it until release,
wherever the pointer goes; `pointer` is the pointer in the element's reference
pixels from its top left, and `resolvedSize` its laid-out size. `UISlider` is
built on this, and a scrollbar thumb is the same along y.

### Sliders, toggles and choices

Sliders and toggles are components on a `UIButton`, a choice names two; their
look is images and text the project parents as usual.

- **`UISlider`** makes its button the track. While a press on it is held,
  `value` follows the pointer between `minValue` and `maxValue`, snapped to
  `step` if set, and each change enqueues a `UIValueEvent`. It places its `fill`
  (owns its `relativeSize.x`) and `knob` (owns its `anchor.x`). Make them the
  track's children with `blocksPointer` off, so a press on the knob is a press
  on the track. A value the game writes shows without an event.
- **`UIToggle`** flips `on` when its button is clicked and enqueues a
  `UIValueEvent` (1 on, 0 off) beside the `UIClickEvent`, in no promised order;
  `on` has flipped by either. Its `knob` sits left when off and right when on
  (owns its `anchor.x` and `pivot.x`), and while on the button rests in
  `onColor` instead of `normalColor` unless `onColor` is clear.
- **`UIChoice`** is one of its `options`, a setting's "< High >" row: a click on
  its `back` or `next` button steps `index` that way, wrapping at the ends, and
  enqueues a `UIValueEvent` carrying the new index and the clicked button's
  `eventId`. It writes the option into its `text` entity's `UIText` each frame it
  is laid out (so the choice is itself a `UIElement` under a canvas), and a
  project that rewrites `options` or `index` is shown at once - when the text is
  the choice itself or its child.

```cpp
subscribe([this](const UIValueEvent& e) {
    if (e.eventId == "volume") setVolume(e.value);
    if (e.eventId == "vsync")  setVSync(e.value > 0.5f);
});
```

## The world on the screen

`ScreenView` (`system/ui/screen_view.h`) is the 3D view one frame draws: the
host's scene rect (on `ctx.chrome`) and the active camera. A behavior gets one
from `screen()`, and the cursor's ray from `pointerRay()`.

- `rayThrough(windowPoint)` - the ray under a window point, in the units
  `InputMap::pointer()` reports; none off the view.
- `toCanvas(world, canvas)` - where a world point lands on a canvas, in its
  pixels from the top left; none behind the camera. An element anchored top
  left at that offset sits over the point - a name over a head.

Both use the host's rect, not the window; they differ in the editor's Play
panel.

## Text input

There is no text field component; a field is a composition
([engine.md](../guides/engine.md#4-what-has-already-been-decided) - no widget
library). The engine gives the pieces: `input().text()` (characters typed this
frame, UTF-32), `input().typed(key)` (an editing key went down or repeated),
`Utf8::append` / `Utf8::previous` (`core/utf8.h`) to edit UTF-8 without
splitting a character, and `measureText` to place the caret.

A field is a `UIButton` (the box; its `UIClickEvent` focuses it), a
left-aligned `UIText` on it, a thin `UIImage` child for the caret, and a
behavior holding the focus and caret offset that, while focused, edits the text
each frame:

```cpp
const FontAsset& font = resources().get(resources().findByName<FontAsset>(text.font));
for (char32_t c : input().text()) {               // insert what was typed
    std::string typed;
    Utf8::append(typed, c);
    text.text.insert(m_at, typed);
    m_at += typed.size();
}
if (input().typed(GLFW_KEY_BACKSPACE) && m_at > 0) {
    const size_t from = Utf8::previous(text.text, m_at);
    text.text.erase(from, m_at - from);
    m_at = from;
}
caret.position.x = PADDING + measureText(
    font,
    std::string_view(text.text).substr(0, m_at),
    text.pixelSize
);
```

`measureText` returns reference pixels, the caret's units. To keep a long name
inside the box, put the text element under a parent with `clipChildren` - an
element's glyphs draw under its parent's clip, not its own. Typed characters
still reach gameplay bound to the same keys, so a field belongs where the game
is not listening - a menu, a pause screen.

## Editor authoring

UI is authored through the inspector, with a card per UI component and full
undo; there are no on-canvas rect handles. The Create menu's "UI" submenu
(Canvas / Panel / Scroll View / Text / Button) parents a new element under the
selected canvas or element. Dragging an element in the Hierarchy reparents it;
a move that leaves it with no `UICanvas` ancestor still happens and raises a
toast.

## Not built

Layout containers (rows / stacks), 9-slice sprites, atlas regions on `UIImage`,
a scrollbar (build one from a held `UIButton` and `UIScroll`'s sizes), controls
beyond slider, toggle and choice, keyboard focus and navigation, UI animation,
world-space UI (pin a label each frame with `ScreenView::toCanvas`), and a
visual 2D edit mode.

---

## How it works inside

### Per-frame flow

`UISystem` runs in the **Transform stage, right after `HierarchySystem`**: UI
layout is the 2D sibling of resolving `WorldTransform`.

```
UISystem::update(FrameContext)
  |-- collect visible UICanvases, sort by sortOrder
  |-- per canvas: walk its hierarchy parent-before-child, carrying the clip rect
  |     and inherited opacity:
  |       resolve UIElement.screenRect; place slider/toggle/choice parts by value
  |       emit quads (UIImage, UIButton) and glyphs (UIText), each tagged with its clip
  |       record hit candidates and blockers still in sight
  |       narrow the clip / offset for scroll / measure content for the children
  |-- resolveInteraction(): topmost blocker wins; button states, UIClickEvent,
  |     toggle/slider/choice changes with UIValueEvent, wheel to a scroll view
  |-- a toggle, slider or choice changed: walk the canvases again so its parts show it
  |-- publish UIDrawData on ctx.ui

RenderSystem::update
  |-- RenderView::build points view.ui at ctx.ui (also on the no-camera path)
  |-- backend: ... -> Composite -> ... -> GLUIPass -> Splash
```

### The draw seam

`UIDrawData` is a backend-agnostic POD: a `UIVertex` stream plus `UIDrawCmd`s (a
vertex range, a clip rect, and the font atlas or picture sampled). A glyph is
marked in the vertex by a corner radius of `UI_TEXT_MARK`, a pictured solid by
its `image` flag, so text, solids and pictures share draw state, and consecutive
runs with one clip and no conflicting atlas or picture merge into one draw call.

`GLUIPass` streams it through the frame's ring and draws each command
alpha-blended, depth off, scissored to its clip; it restores the scissor itself,
since the backend's between-pass reset does not. Font atlases upload in
`GLView::sync` to their own table keyed by `FontHandle` (fonts are not
`TextureAsset`s). The UI blends in display space, so an sRGB picture is encoded
back before the tint. The `shaders/ui` program reads both marks per fragment;
`UI_TEXT_MARK` comes from the prelude.

### Fonts

`bakeFontSDF` (`vkm_tools`) renders glyphs across the thread pool into a square
atlas with `FontAsset::MIP_LEVELS` levels and a gutter so no level mixes glyphs,
and records the face's kerning pairs. A distance field cannot hold a corner
sharper than its texels, so the bake size follows the display (`uiFontPixels`,
from `WindowManager::displayHeight()`). A `FontAsset` is self-contained - no
texture handle - and `ResourceManager::swap` leaves its slot out, so **fonts
survive scene load with no re-bake**.

### Serialization

Each UI component has a row in `VKM_SCENE_COMPONENTS` with its plain fields
reflected. `UIImage` is an `R` row: its texture is saved by name and listed in
the scene's `assets` block. `UISlider`, `UIToggle` and `UIChoice` are `E` rows:
the part entities they name are saved as slots. A scroll view's `offset` is saved.

### Key files

- `src/engine/ecs/component/ui/` - the components, one header each; `UIShape` (`ui_shape.h`) is shared by `UIImage` and `UIButton`
- `src/engine/system/ui/ui_system.{h,cpp}` - `UISystem`
- `src/engine/system/ui/ui_draw_data.h` - `UIVertex` / `UIDrawCmd` / `UIDrawData` (the engine -> backend contract)
- `src/engine/system/ui/ui_events.h` - `UIClickEvent`, `UIValueEvent`
- `src/engine/system/ui/screen_view.{h,cpp}` - `ScreenView`
- `src/engine/system/ui/text_layout.h` - `walkGlyphs` / `measureText`
- `src/engine/core/utf8.h` - `Utf8::next` / `append` / `previous`
- `src/engine/resource/asset/font_asset.h` - `FontAsset`
- `src/tools/loader/font_baker.{h,cpp}` - `bakeFontSDF`, `uiFontPixels`
- `src/backend/opengl/pass/gl_ui_pass.{h,cpp}` - `GLUIPass`
- `shaders/ui/` - the UI shader
