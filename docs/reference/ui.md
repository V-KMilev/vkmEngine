# UI System

In-game, screen-space UI modeled as ECS. A UI element is an entity with plain-struct
components; `UISystem` lays them out in 2D, hit-tests the pointer, and builds a batched draw
list that `GLUIPass` draws on top after Composite. It runs in every host that runs
`setupEngineApp` and uses **no ImGui** - ImGui is the editor's own tooling.

## The component model

A `UICanvas` roots a UI layer over the viewport; several draw - and hit-test - in ascending
`sortOrder`. Every element under one carries a `UIElement`, the 2D analog of `Transform`,
and adds what it is: a `UIImage`, a `UIText`, a `UIButton`, a `UIScroll`, a `UIStack`, or a
control riding a button. `visible` and `opacity` pass down the whole subtree. Each header in
`ecs/component/ui/` documents its fields. Being entities, UI elements get nesting, the
editor's inspector and undo, and scene serialization for free.

**Two structural rules**, both of which leave a full card and an empty viewport
([the editor names them](editor.md#a-card-names-what-its-component-is-waiting-for)): an
element with no `UICanvas` ancestor is never visited - strictly an ancestor, so a `UIElement`
on the canvas entity itself is never resolved (`hasCanvasAncestor`,
`ecs/component/ui/ui_canvas.h`); and a `UIImage` / `UIText` / `UIButton` / `UIChoice` on an
entity with no `UIElement` is never reached.

## Layout: anchor / pivot

`anchor` and `pivot` are normalized `0..1` from the top left: `anchor` picks the point of the
parent rect to pin to, `pivot` the point of the element that lands there. `position` and `size`
are in the canvas's reference pixels, scaled with the canvas, so an element keeps its corner and
proportions at any resolution. `relativeSize` is the share of the parent's size an element adds
to `size`: `(1, 1)` with zero `size` is a full-screen scrim, `(1, 1)` with a negative `size` the
parent less a margin.

```
sizePx         = max(size*scale + relativeSize*parentSize, 0)
screenRect.pos = parentPos + anchor*parentSize + position*scale - pivot*sizePx
```

**A `UIStack` lays its children out one after another** - a column, or a row - so a list of
settings or a HUD's lines need no positions worked out. Each shown child takes a slot its own
size along the axis and the stack's whole extent across it, and is placed in the slot as in any
parent: one made by `UIElement::at` fills it. A hidden child takes no slot, so the rest close up.
The stack keeps its own size.

## Menus that pause the game

A `UICanvas` with `pausesGame` holds the world still while it shows - `onUpdate` and the ticks
stop, as under `Clock::setPaused`, but through a hold of its own (`Clock::setMenuShown`), so
closing the menu never resumes a pause the editor made - and frees the cursor, the game's own
`CursorMode` kept for when it closes (`WindowManager::setCursorFree`). Its `toggleAction` shows
and hides it on an action's press. So a pause menu is a canvas with the two set and a Resume
button that hides it, and no hook; a `restartScene` from it takes the canvas with the old world,
so the new one runs. Both act only while the world runs, never in the editor's Edit mode, and
the hold only offline: in a session the world is the server's.

## Clipping and scrolling

`UIElement::clipChildren` bounds what descendants draw to the element's rect, intersected with
every clipping ancestor. It bounds the **pointer** too: a button clipped out of sight is not
hit and does not block.

`UIScroll` lays its children out against its rect shifted by `offset`, and always clips;
parent the content under it as usual. **It measures its own content**: `contentSize` is how far
what the descendants drew reaches, a subtree that clips itself counting as its rect. `offset`
is clamped to `range()` every frame, so writing a large number means "the bottom".

The wheel scrolls the innermost scroll view under the pointer that no other blocker covers,
else the one holding the topmost blocker, on whichever axis overflows, vertical first. Over a
blocker `input().wheel()` reads zero, so scrolling a list is never also a gameplay action.

## Text

`UIText::text` is UTF-8, drawn from a `FontAsset`: a signed-distance atlas, so one bake serves
a wide range of `pixelSize`. The font is named by **asset name**, not a handle
([Fonts](#fonts) says why); a name nothing answers to draws nothing. A bake covers printable
ASCII, Latin-1 and `FontAsset::MARKS`; any other character draws nothing.

`UIText::wrap` says where a line breaks. `valign` places the whole block, which is not bounded
to the rect - clip above it if it may overflow. Measuring and drawing walk a line the same way
(`walkGlyphs`, `system/ui/text_layout.h`), so `measureText` agrees with what draws.

**A button's label rides the button's own entity**: image, button and text draw from one rect
in that order, so a centered `UIText` on the button is its label - no child needed.

## Interaction

The topmost element under the pointer that **blocks** wins it: one that draws (a `UIImage` or
`UIButton`) with `UIElement::blocksPointer` on, the last in painter order across canvases in
sort order. So an opaque panel over the HUD takes the click from the buttons beneath; turn
`blocksPointer` off for an overlay meant to be clicked through, a vignette or a crosshair. Text
never blocks. Only the topmost blocker hovers or presses, and a press and release over the
same button **enqueues a `UIClickEvent`** naming it, which a behavior hears through `onClick`
([recipes.md](../recipes.md#screens)).

**A click on a menu never fires a weapon.** A press of `UI/Click`'s mouse button that begins
over a blocker belongs to the UI: it reads as down for `UI/Click` and up for every other action
until released, so gameplay code never has to ask. Input is read before the UI lays out, so it
asks the panels as last laid out (`InputMap::setPointerBlockers`), at the pointer where it is
now: a click the frame the pointer arrives on a panel is the panel's. `input().pointerOverUI()`
answers the same question for anything else, such as hiding a crosshair.

**Mouse-look turns hit-testing off.** With the cursor disabled (`CursorMode::Disabled`) its
position points at nothing, so nothing hovers, presses, scrolls or blocks. In the editor,
while its chrome holds the pointer (`HostChrome::capturePointer`), nothing hovers, presses or
scrolls; the UI still lays out, draws and resolves `UIDrawData::pointerTarget`, the topmost
blocker the editor's picker reads.

### Sliders, toggles and choices

`UISlider` and `UIToggle` sit on a `UIButton`, and a `UIChoice` names two, its back and next
arrows, which stop at either end; their look is images and text the project parents as usual.
Each frame the system places the parts a control names by its value - a slider's fill and knob,
a toggle's knob, a choice's text - and owns the fields of them it writes (each header says
which). Make a slider's or toggle's parts its button's children with `blocksPointer` off: a
part that blocks takes the press from the button. A change the pointer makes enqueues a
`UIValueEvent`, which a behavior hears through `onChange`; a value the game writes shows
without one.

### The engine's controls

`UI::canvas`, `UI::panel`, `UI::text`, `UI::column`, `UI::row`, `UI::button`, `UI::slider`,
`UI::toggle` and `UI::choice` (`ecs/component/ui/ui_build.h`) each build one of these from the
components above, parts and all, under a parent and in the look a `UI::Style` gives - its font,
colors, rounding and, if it sets one, a shadow under every text, so a HUD reads over the scene.
They add no control of their own: a project builds others from the same components, and the
editor's Create menu builds its controls with these. [recipes.md](../recipes.md#screens) has them
in use.

## The world on the screen

`ScreenView` (`core/frame/screen_view.h`), the view one frame draws, turns a window point into a
ray into the world (`rayThrough`) and a world point into a place on a canvas (`toCanvas`): a
name over a head ([recipes.md](../recipes.md#screens)). A behavior gets one from `screen()`, and
the cursor's ray from `pointerRay()`. Both use the host's scene rect, not the window; they
differ in the editor's Viewport.

## Text input

There is no text field component; a field is a composition
([decisions.md](decisions.md#what-has-been-decided) - no widget library). The engine gives the
pieces: `input().text()`, the characters typed this frame as UTF-32 with the layout and Shift
applied; `input().typed(key)`, whether an editing key
([input.md](input.md#keys-and-buttons)) went down or auto-repeated; `Utf8::append` and
`Utf8::previous` (`base/encoding/utf8.h`) to edit UTF-8 without splitting a character; and
`measureText` to place the caret. A field is a `UIButton` (the box; its `UIClickEvent` focuses
it), a left-aligned `UIText` on it, a thin `UIImage` child for the caret, and a behavior holding
the focus and caret offset that, while focused, edits the text each frame:

```cpp
const FontAsset& font = resources().get(resources().findByName<FontAsset>(text.font));
for (char32_t c : input().text()) {               // insert what was typed
    std::string typed;
    Utf8::append(typed, c);
    text.text.insert(m_at, typed);
    m_at += typed.size();
}
if (input().typed(Key::Backspace) && m_at > 0) {
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

`measureText` returns reference pixels, the caret's units. To keep a long name inside the box,
put the text element under a parent with `clipChildren` - an element's glyphs draw under its
parent's clip, not its own. Typed characters still reach gameplay bound to the same keys, so a
field belongs where the game is not listening - a menu, a pause screen.

## Not built

A stack that grows to fit its children or spreads them along its length, 9-slice sprites, atlas
regions on `UIImage`, a scrollbar
(build one from a held `UIButton` and `UIScroll`'s sizes), controls beyond slider, toggle and
choice, keyboard focus and navigation, UI animation, world-space UI (pin a label each frame with
`ScreenView::toCanvas`, as `examples/the_lab/src/lab_sign.h` does), and a visual 2D edit mode.

---

## How it works inside

`UISystem` (its class says where it runs and what it reads) walks the visible canvases parent
before child, carrying the clip and the inherited opacity down, then settles the pointer; a
control that changed is laid out again, so its parts show the change the frame it happens.

### The loading screen

A world arriving - a load, a restart, a server's next scene - is covered by `LoadingSystem` from
the frame it arrives until it is ready, then the cover fades; the class says what "ready" waits
for, and [rendering.md](rendering.md#bakes) what the bakes are. The cover is three layers: the
backend's water; a draw list of its own (`ctx.cover`, from `UISystem::layOutCover`), drawn through
`GLUIPass`, holding the project's screen, the line naming the step under way and a tip; and over
both the backend's bar. One pass draws all three ([rendering.md](rendering.md#the-passes-fixed-order)). While it
shows it takes the pointer: nothing under it, or on it, is hovered, pressed or scrolled.

A project adds tips, a prefab of UI canvases over the water, and whether the water shows (else
black) in its `loading` block ([io.md](io.md#projects-and-their-roots)), which each host hands to
`LoadingSystem::setScreen`. A load a behavior asks for waits behind the cover (`LoadExpected`),
so its stall shows the cover, not a frozen frame. When the world under it starts to tick is
[architecture.md](architecture.md#the-fixed-step)'s, and when a host authoring the world is
covered is `LoadingSystem`'s.

### The draw seam

`UIDrawData` (`core/frame/ui_draw_data.h`) is the UI's whole engine-to-backend contract: one
stream of quads, a rectangle each, and its commands, each a clip rect and the atlas or picture it
samples, so text, solids and pictures share draw calls. `GLUIPass` draws each command alpha-blended, depth off,
scissored to its clip; the scissor stays on through the pass, and the state reset the backend
runs before every pass (`GLBackend::passState`) turns it off again.

### Fonts

A font is baked, not cooked or loaded with a scene: each host with a window bakes the engine's
(`DEFAULT_UI_FONT`) as it starts and the project's own (`Project::fonts`) as it opens the
project, at a size that follows the display (`uiFontPixels`), since a distance field cannot
hold a corner sharper than its texels. A `FontAsset` holds its own atlas and
`ResourceManager::swap` leaves fonts in place, so **fonts survive a scene load with no
re-bake** - which is why a `UIText` names its font.

## Key files

- `src/engine/ecs/component/ui/` - the components, one header each
- `src/engine/system/ui/ui_system.h` - `UISystem`
- `src/engine/ecs/component/ui/ui_build.h` - the `UI::` controls and `UI::Style`
- `src/engine/core/frame/ui_draw_data.h` - `UIDrawData`, the engine-to-backend contract
- `src/engine/core/frame/ui_events.h` - `UIClickEvent`, `UIValueEvent`
- `src/engine/core/frame/screen_view.h` - `ScreenView`
- `src/engine/system/ui/text_layout.h` - `walkGlyphs`, `measureText`
- `src/engine/system/loading/loading_system.h` - `LoadingSystem`
- `src/loader/font_baker.h` - `bakeFontSDF`, `uiFontPixels`
- `src/backend/opengl/pass/overlay/gl_ui_pass.h` - `GLUIPass`
