# UI System

In-game, screen-space UI modelled as ECS. A UI element is an entity with
plain-struct components; the `UISystem` resolves 2D layout, hit-tests the
pointer, and builds a batched draw list that rides the **`RenderView` seam**
into the backend, where one `GLUIPass` draws it on top after Composite.
It runs identically in every host that runs `setupEngineApp` - `vkm_runtime`,
`vkm_editor` and `vkm_server` - and pulls in **no ImGui**: ImGui stays the
editor's own tooling, never the shipped game UI.

> The one idea: **UI is just more ECS.** Because a UI element is an entity,
> it inherits the entity hierarchy (nesting), the editor's inspector / selection
> / undo, and scene serialization with almost no UI-specific machinery.

## The component model

| Component | Holds | Notes |
|-----------|-------|-------|
| `UICanvas` | reference height, scale mode, sort order, visible | Root of a UI layer; spans the viewport. A scene can have several (HUD, menu); they draw - and hit-test - in ascending `sortOrder`. Put it on a root entity and parent `UIElement`s under it. |
| `UIElement` | anchor, pivot, position, size, relativeSize, visible, blocksPointer, clipChildren (+ resolved `screenRect`) | The 2D analogue of `Transform`. Nesting reuses the **normal entity hierarchy**; `visible = false` skips the element and its whole subtree. `blocksPointer` (on by default) decides whether it stops a click reaching what is behind it, and `clipChildren` (off by default) whether its rect bounds what its descendants may draw - both below. |
| `UIImage` | colour, shape, texture | A filled quad over the element's rect. `shape` (a `UIShape`) gives it rounded corners, a border stroke and a top-to-bottom gradient; at its defaults the quad is the flat tint. A `texture` makes it a picture - an icon, a logo - stretched over the rect, multiplied by the tint and cut by the same corners. The texture is a handle, saved by name, like a material's maps. |
| `UIText` | text, font, pixel size, colour, align, valign, wrap | SDF text, aligned in the rect on both axes. Newlines break; `wrap` breaks on width too. The font is named, not handle-referenced (see Text). |
| `UIButton` | normal/hover/pressed/disabled tints, shape, eventId, interactable (+ resolved `state`, `held`, `pointer`, `resolvedSize`) | Hit-tested topmost-wins; the `UISystem` drives its visual `state` and fires `UIClickEvent`. A label rides the **same entity** - see Text. It is also the drag primitive - see Interaction. |
| `UIScroll` | offset, wheel step (+ resolved `contentSize` / `viewSize`) | Makes the element a window onto its children. Always clips. |

`screenRect` (on `UIElement`, a `UIRect`), `state`, `held`, `pointer` and `resolvedSize` (on `UIButton`) and
`contentSize` / `viewSize` (on `UIScroll`) are resolved every frame, so they are
**not** reflected and do not serialize. A scene file is what was authored, and
how tall the content happened to be on some frame is not.

Two structural requirements follow from the walk, and the editor names both on
the card rather than leaving them to be discovered: an element with **no
`UICanvas` ancestor** is never visited (the walk is only ever seeded from a
canvas), and a `UIImage` / `UIText` / `UIButton` on an entity with **no
`UIElement`** is never reached (`resolveElement` returns before it looks for
any of the three). Both leave a card that renders in full and a viewport with
nothing in it - see
[the editor](editor.md#a-card-names-what-its-component-is-waiting-for).

The first requirement is **strictly** an ancestor: the seed is
`forEachChild(canvas)`, so a `UIElement` put on the canvas entity itself is
never resolved either. `hasCanvasAncestor` in `ecs/component/ui/ui_canvas.h` is
the one definition of that rule, the way `findActiveCamera` and `findKeyLight`
are of theirs, because the Inspector card and the interactive reparent both ask
it and must not disagree over the same entity.

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
sizePx         = max(size*scale + relativeSize*parentSize, 0)
screenRect.pos = parentPos + anchor*parentSize + position*scale - pivot*sizePx
```

`relativeSize` is the share of the parent's size an element takes on top of
`size`, zero by default. It is what fills or spans a parent at any resolution:
`(1, 1)` with a zero `size` is a full-screen scrim on a canvas, `(1, 0)` with a
height is a bar the width of its panel, and `(1, 1)` with a negative `size` at a
centred anchor is the parent less a margin on every side. The share is of the
parent's resolved pixels, so it is not scaled again; `size` and `position` stay
in reference pixels. A size that resolves below zero is zero.

`UIElement::at(anchorPivot, position, size)` builds the case everything actually
authors: one normalised point used as both anchor and pivot, because a top-left
readout pins its own top-left and a centred panel its own centre. The fields stay public,
so an element that wants its centre on its parent's corner still says so.

Top-left HUD element: `anchor = pivot = (0,0)`. Screen-centred: `(0.5,0.5)`.
There are no layout containers and no 9-slice.

## Clipping

`UIElement::clipChildren` makes the element's rect bound what its descendants
may draw. The clip descends with the walk and **intersects** at every element
that sets it, so an inner panel can never draw outside an outer one that already
narrowed it, and the canvas seeds the walk with the viewport - nothing escapes
the view even before anything asks to clip.

It bounds the **pointer** as well as the pixels. A button laid out past the edge
of its panel, or scrolled out of sight, is not a hit candidate and does not
block: a click nobody can see the button inviting must not land on it.

The clip is carried on the draw command (`UIDrawCmd::clip`) rather than on the
vertices, because clipping is a change of draw *state* like the font. Two runs
that agree on it still merge into one draw call; two that do not cannot. The
backend sets a scissor box per command - and puts it back at the end of the
pass, being the one piece of GL state the backend does not reset between passes.

## Scrolling

`UIScroll` on an element makes it a window onto its children: they lay out
against its rect shifted by `offset`, so the element stays where the layout put
it and the content moves underneath. Parent the content under it as usual -
there is no special container.

A `UIScroll` clips whether or not `clipChildren` is set. Scrolling without
clipping would leave the content you scrolled away from drawn over everything
above the panel, so it is one rule rather than two fields to keep in step.

**It measures its own content.** `contentSize` is how far what the descendants
*drew* reaches, taken during the layout walk, so nothing has to be told how tall
the content is and nothing can be told wrongly. An image or a button counts its
rect, a text counts the block of lines it laid out, and an element that draws
nothing counts nothing - a rect is a place to put things, and a layout box or a
text box with room to spare would otherwise scroll a list past its own last row.
A subtree that itself clips reports only its own rect, which is what stops a
list inside a list growing the one around it forever. `offset` is clamped to
`range()` (`contentSize - viewSize`, never negative) every frame, once the
frame's content is measured, so a caller can write a large number to mean "the
bottom" without knowing how tall anything is. Both sizes are in the canvas's
reference pixels, so `offset / range()` is the fraction a project's own
scrollbar needs, with no unit conversion.

The wheel goes where a click would. Whatever is on top under the pointer decides:
the innermost scroll view whose window the pointer is inside and that lies in or
over the topmost blocker takes it - so a list inside an opaque panel scrolls
even where it draws nothing of its own, a gap between rows still scrolls, and an
inner list takes it while the one around it stays put. Failing one, the wheel
travels up from the topmost blocker to the nearest scroll view holding it, so a
pause menu drawn over a list takes it away from the list by the same rule it
takes the click. The UI claims the wheel the way it claims a press: over a
blocking element `input().wheel()` reads zero, so a list scrolling under the
pointer is never also a weapon switch or a zoom, and `UISystem` reads every
notch through `uiWheel()`. What it does lands on the *next* frame's layout: which view that
is, is only known once the walk is over, and by then this frame's rects are
already vertices. One wheel drives whichever axis has
somewhere to go, vertical first, so a row that only scrolls sideways turns with
the same notch and the component needs no axis flags to keep in step with its
content.

## Text: SDF fonts

`FontAsset` is a `ResourceManager` asset holding the single-channel
signed-distance atlas **as pixels**, plus per-glyph metrics (`FontGlyph`), the
face's ascent / descent / lineHeight, and the face's kerning pairs. `bakeFontSDF` (in
`vkm_tools`) renders each glyph with `stbtt_GetGlyphSDF`, glyphs spread across
the thread pool since the field is the whole cost and a host bakes before its
first frame, packs them with
`stb_rect_pack` into the smallest square atlas that holds them, and records
every pair the face kerns (`stbtt_GetGlyphKernAdvance`, which reads `GPOS` as
well as `kern`) as a sorted table. Because the atlas stores distance, one bake
scales over a wide range: the UISystem scales the metrics by the text's
`pixelSize` times the canvas scale over the font's `pixelHeight`, and the shader divides the field by how fast it
changes across a screen pixel, which is the distance to the edge in pixels -
the edge is anti-aliased over exactly one pixel at any size. `valign` places
the block Top / Middle / Bottom within the element rect.

**What a bake covers.** Printable ASCII, the printable half of Latin-1
(U+00A0-U+00FF: accents, the degree sign, currency, the no-break space) and the
marks UI text reaches for past it - en and em dashes, curly quotes, the bullet,
the ellipsis and the euro sign (`FontAsset::MARKS`). `UIText::text` is UTF-8 and
is decoded as it is laid out; a character the bake does not cover draws nothing
and moves nothing, and a malformed byte is skipped without costing the text
after it.

**Small text.** Text is usually drawn far smaller than it was baked - a 12px
label from a 64-128px bake - so the atlas is minified, and a single level
sampled at that ratio skips texels and drops strokes. It has a mip chain of
`FontAsset::MIP_LEVELS` levels, and the baker leaves a gutter of the smallest
level's texel between glyphs so no level mixes two of them. The shader samples
one level finer than the footprint asks for: the field is a smooth slope across
its spread (never narrower than that gutter), so the finer level still
reconstructs it, where the coarser level's averaging thins every stem. Each
line's start and baseline are rounded to whole pixels, which keeps a small
label's baseline and stems sharp; the glyphs along a line keep their fractional
advances, so the spacing is the face's own.

**One walk.** `walkGlyphs` (`system/ui/text_layout.h`) steps a line codepoint
by codepoint, kerned, and is the only thing that does: wrapping, alignment and
the glyph quads all go through it, and so does `measureText`, which is what a
project measures a string with - see [Text input](#text-input). A width
measured and the glyphs drawn cannot disagree.

**Wide, not unlimited.** A distance field cannot represent a corner sharper than
its own texel grid, so past roughly three times the baked height the corners
start rounding off - which is why the bake size follows the display rather than
being a constant. `ensureDefaultUIFont` (`app/engine_app.h`) scales it from
`WindowManager::displayHeight()` against a 1080-line reference, from 64px up to
a capped 128px; the monitor rather than the window, because the bake happens
once and has to cover whatever that window is later resized into. The
distance-field spread is derived from that height, and the atlas dimension from
the glyphs the baker actually rendered at it - the smallest power of two that
packs them, doubled until it does - so no estimate can fall short of what a
height needs.

The asset is deliberately **self-contained** - no handle into the texture slot.
Fonts are runtime-baked and never enter scene files, so a staging graph built
from a scene has no font slot to put in place of the live one;
`ResourceManager::swap` leaves the `FontAsset` slot out of the trade for exactly
that reason, and self-containment is what makes that safe. The consequence: **fonts
survive scene load with no re-bake**, and the bake runs once at startup.

### Lines

A newline in `text` always breaks; `UIText::wrap` breaks on the element's width
as well, at the last space that fits, and mid-word where a single word is wider
than the rect - which is the only way to bound an unbroken token inside one.
Lines are spaced by the face's own `lineHeight`, and `valign` places the whole
block. Nothing bounds the block to the rect, so a paragraph taller than its box
overflows unless something above it clips.

A **button's label rides the button's own entity**: `resolveElement` emits
image, button and text off one resolved rect, in that order, so a `UIText`
beside a `UIButton` draws on top of it. Centre it with `align = Center` and
`valign = Middle` and it needs no rect, no child and no hand-tuned offsets.

`UIText` references its font by **asset name**, not a handle: names are the
serializable asset identity, so the component stays plain data, and the
per-frame `findByName` is O(1). A name nothing answers to draws nothing at all -
`emitText` returns on the null handle - so the inspector's UI Text card, which
picks the font from a combo of the loaded fonts, reports an unresolved name in
red.

## Interaction

The UISystem hit-tests the pointer (in viewport-local pixels) during the walk
but only **records candidates**; once the frame's draw list is complete,
`resolveInteraction()` picks the **topmost** one under the pointer (the last in
painter order, across canvases in sort order).

A candidate is anything that *blocks* the pointer, not just a button: an element
that draws - a `UIImage` or a `UIButton` - and whose `UIElement::blocksPointer`
is true, which it is by default. That is what makes a pause menu behave like
one. An opaque panel laid over the HUD wins the pointer against the buttons
underneath it by the same rule that decides between two overlapping buttons, and
a click on the panel reaches neither. Turn `blocksPointer` off for an overlay
meant to be clicked through - a vignette, a crosshair, a damage flash. Text
never blocks: a label over a button is a caption on it.

Only the topmost blocker hovers or presses - overlapping buttons never light up
together - and when it is not a button, nothing is armed at all. A press starts
a click candidate on the topmost button; releasing over that same button
**enqueues a `UIClickEvent`** through the frame's `EventBus` (`ctx.events`).

A press that begins over a blocker is the UI's, which is how a shot does not go
through an open menu. `UISystem` tells the `InputMap` whether a blocker is under
the pointer, and the next `InputMap::update` gives a press of any mouse button
`UI/Click` is bound to that begins there to the UI: it reads as down for
`UI/Click` and as up for every other action until it is let go, so it never
reaches a gameplay action or a tick's command, and no gameplay code asks first.
`input().pointerOverUI()` answers the same question for anything else that wants
it, such as hiding a crosshair over a panel. `UIDrawData::pointerTarget` names
the topmost blocker itself, for the editor's picker, and is resolved whenever the
cursor is free, whoever holds it: a host holding the pointer (`HostChrome`) stops
the UI's hover, presses and wheel, not the question of what is under it. The
editor holds it whenever the viewport is not the game's - in Edit mode and while
ejected - so an author's wheel over a scroll view does not edit its saved offset.

A button also says where a press that began on it has been carried: `held` is
true from that press until it is let go, wherever the pointer goes meanwhile, and
`pointer` is the pointer in the element's own reference pixels, from its top-left,
with `resolvedSize` the element's laid-out size in the same pixels - `size` plus
whatever `relativeSize` took from its parent. That makes the button the one drag
primitive. A slider is a clear button over its track that sets
`value = pointer.x / resolvedSize.x` while `held`, with its fill and knob letting
the pointer through; a scrollbar thumb is the same along y. No project converts
pointer spaces or decides who a press belongs to.

While the cursor is disabled for mouse-look (`CursorMode::Disabled`) nothing is
hit-tested: the position GLFW reports then is a virtual one that drifts with the
view, and a HUD panel it crossed would otherwise take the gun's click.

Gameplay reacts to a click the same way it would to any event:

```cpp
subscribe<UIClickEvent>([this](const UIClickEvent& e) {
    if (e.eventId == "play") startGame();
});
```

In the editor the pointer is shared with the editor's own chrome, so
`EditorSystem` states once a frame on `ctx.chrome` whether the host holds it -
its panels, or everything while the view is ejected - and `UISystem` reads
that answer. The editor's fly camera is told the panels' half directly
(`CameraControllerSystem::setCapture`), since an ejected view is exactly when it
should fly. While the chrome's capture is set
the layout still runs and the overlay still draws, but nothing hit-tests - a
click aimed at the viewport's tool strip, view bar, playbar or a gizmo does not also press
the game button behind it. A press or a release the chrome takes also disarms
whatever button a press armed, so a press dragged onto a panel and let go there
is over, not waiting to become a click on the next release. The runtime never
sets it.

## Text input

There is no text field component: a field is a composition, like a slider
([engine.md](../guides/engine.md#4-what-has-already-been-decided) - no widget
library). What the engine gives it is the input and the measuring: `InputMap`
hands over the characters typed this frame (`input().text()`, UTF-32) and which
editing keys went down or auto-repeated (`input().typed(key)`); `Utf8::append`
and `Utf8::previous` (`core/utf8.h`) edit a `UIText`'s UTF-8 without splitting a
character; and `measureText` (`system/ui/text_layout.h`) says where a caret
goes, through the same kerned walk the glyphs are drawn by.

A field is then a `UIButton` (the box, and what a click focuses), a left-aligned
`UIText` on it, a thin `UIImage` child for the caret, and the focus and caret
offset as fields on a behavior:

```cpp
// m_focused is set by the box's UIClickEvent, subscribed in onStart.
void NameField::onRealtimeUpdate(float dt) {
    UIText&    text  = scene().get<UIText>(m_box);
    UIElement& caret = scene().get<UIElement>(m_caret);
    caret.visible = m_focused && std::fmod(m_clock += dt, 1.0f) < 0.5f;
    if (!m_focused) return;

    for (char32_t c : input().text()) {
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
    if (input().typed(GLFW_KEY_LEFT)) m_at = Utf8::previous(text.text, m_at);
    if (input().typed(GLFW_KEY_RIGHT) && m_at < text.text.size()) Utf8::next(text.text, m_at);
    if (input().typed(GLFW_KEY_ENTER)) m_focused = false;

    const FontAsset& font = resources().get(resources().findByName<FontAsset>(text.font));
    caret.position.x = PADDING + measureText(font, std::string_view(text.text).substr(0, m_at),
                                             text.pixelSize);
}
```

The caret's `position` is in the same reference pixels as `pixelSize`, so
`measureText` needs no conversion; put the text element under one with
`clipChildren` - an element's own glyphs draw under its parent's clip, not its
own - so a long name does not run out of the box. Two things stay the
project's: a character typed into a field still reaches whatever gameplay reads
the same keys as actions, so a field belongs where the game is not listening -
a menu, a pause screen - and Tab, focus order and controller navigation are not
built (see below).

## Editor authoring

Because UI elements are entities, the editor support is mostly inherited:

- **Hierarchy** lists every entity, whatever it carries, so UI entities (which
  carry no `Transform`) appear in the tree, and dragging one
  onto a canvas reparents it like anything else. The world-preserving re-base
  the interactive reparent does is simply skipped for an entity with no
  `Transform` - a UI element is placed in screen space by its canvas, so there
  is no world pose to keep. A move that leaves an element with no `UICanvas`
  ancestor still happens, and is reported with a toast, because that is what
  stops it being drawn.
- **Inspector** has a card per UI component (`drawUI*Section`), built from the
  shared `prop*` widgets; Add / Remove / field-edit all route through the
  command stack, so authoring is fully undoable. The UI Image card picks its
  texture from the loaded textures with the same picker a mesh's material
  uses. The UI Image and UI Button
  cards draw the same `UIShape` rows - corner radius, border and its colour,
  gradient and its bottom colour - with each colour row shown only while the
  width or the toggle that uses it is set.
- **Add Component** menu has a "UI" group; the **Create** menu has a "UI"
  submenu (Canvas / Panel / Scroll View / Text / Button) that adds a
  `UIElement`/`UICanvas` instead of a `Transform` and parents a new element
  under the selected canvas/element. Scroll View is a panel plus a `UIScroll` -
  a backing image as well, because a scroll view with nothing drawn on it is an
  invisible hole an author cannot see the edge of.
- The **UI Scroll** card shows the measured content and view sizes rather than
  offering them: they are resolved every frame, and "why will this not scroll"
  is answered by the content being no bigger than the window.

There is no visual on-canvas editor (rect handles); UI is authored through the
inspector.

## Not built

Layout containers (rows / stacks), 9-slice sprites, a texture atlas region on
`UIImage` (a picture is the whole texture), a scrollbar or slider widget (a
project builds one from a held `UIButton` and, for a scrollbar, `UIScroll`'s
measured sizes - the engine has no widget library and is not growing one),
keyboard focus and navigation (a field's focus is a flag on the project's
behavior - see Text input - and nothing moves it between fields), UI
animation, world-space / diegetic UI, and a visual 2D edit mode.

---

## How it works inside

Everything above is what a project writes. What follows is how the
engine answers it, for whoever maintains that half.

### Per-frame flow

`UISystem` runs in the **Transform stage, right after `HierarchySystem`** - UI
layout is a screen-space transform resolve, the 2D sibling of resolving
`WorldTransform`. It runs in every host `setupEngineApp` builds.

```
UISystem::update(FrameContext)
  |-- read pointer, wheel and this frame's press/release edges off ctx.input
  |-- collect visible UICanvases, sort by sortOrder (entity index breaks ties)
  |-- for each canvas, in order:
  |     derive a uniform scale from the viewport (ScaleWithHeight)
  |     walk its hierarchy parent-before-child, carrying a clip rect that
  |     starts as the viewport (skipping invisible subtrees):
  |       resolve UIElement.screenRect from anchor/pivot/position/size x scale
  |       emit a quad per UIImage, per UIButton, glyphs per UIText - each
  |         command tagged with the clip it draws inside
  |       record a hit candidate per UIButton, and a blocker per drawn
  |         element, unless the clip put it out of sight
  |       narrow the clip for the children where the element clips, offset
  |         their parent rect where it scrolls, and measure what they filled
  |-- resolveInteraction(): topmost candidate wins the pointer; states +
  |     button quad colours settled; UIClickEvent fired on click; the wheel
  |     given to the innermost scroll view entered after the topmost blocker,
  |     else handed up from that blocker to the scroll view holding it
  |-- publish the batched UIDrawData on ctx.ui   (mirrors ctx.visibility)

RenderSystem::update
  |-- RenderView::build copies ctx.ui into view.ui (camera-independent: it
  |   survives the no-camera path, so a HUD/menu draws with nothing 3D in view)
  |-- backend.render(view) -> ... -> Composite -> GLUIPass -> Splash
```

The hand-off is the same idiom the `VisibilitySystem` uses: the UISystem owns
the buffer and points `ctx.ui` at it; `RenderView` borrows the same pointer. No system
reaches into another.

### The draw seam

`UIDrawData` is a backend-agnostic POD: a flat `UIVertex` stream (screen-pixel
position, uv, straight RGBA, and for a solid its quad's size, corner radius,
border width and border colour, and whether it samples a picture) plus
`UIDrawCmd`s (a vertex range + a clip rect, the `FontHandle` whose atlas the run
samples when it holds text, and the `TextureHandle` of the picture it samples
when it holds one). A glyph marks itself in its vertex - a negative corner
radius, which no solid has - and so does a pictured solid, so text, flat solids
and pictures are not different draw state. A command is a change of draw state
rather than a widget: consecutive runs that share a clip, sit next to each other
in the buffer and sample no two different atlases and no two different pictures
merge, so a screen of forty solid panels is one draw call rather than forty, and
so is a column of labelled buttons, and so is a row of them with the same icon. `RenderView` carries
it the way it carries the 3D objects, which keeps backends interchangeable - the GL
backend is its one consumer, and nothing in the frontend is GL-specific.

### Backend: GLUIPass

`GLUIPass` is appended **after Composite**, second to last in the pass list -
only the splash draws over it. It binds the same
backbuffer rect, streams `UIDrawData` into a dynamic vertex buffer, and draws
each command under an orthographic projection - alpha-blended, depth off, and
scissored to the command's clip rect, rounded outward to whole pixels so a clip
edge half-way across a pixel keeps the pixel rather than cutting it off. The
scissor is the one piece of GL state this pass has to put back itself: the
backend resets depth, blending and culling between passes and not that.
`GLView::sync` uploads font atlases into their own table (keyed by
`FontHandle` - fonts are not `TextureAsset`s), and the pass resolves one per
command, binding a texel of zero coverage where there is none. A command's
picture goes to a second unit from the ordinary texture table; one still loading
or failed shows the missing-texture checker, as a material's map would. The UI
blends in display space, so a picture stored as sRGB - whose sampling returns
linear light - is encoded back before the tint multiplies it. The `shaders/ui`
program branches per vertex: Solid (a quad rounded, bordered and faded from what
its vertices carry) or Text (SDF coverage), told apart by `UI_TEXT_MARK`, which
the prelude hands the shader. It is a no-op when the draw list is empty, so
non-UI scenes pay nothing.

### Serialization

Each UI component's authored fields are reflected (`VKM_REFLECT`, with
`VKM_ENUM_NAMES` for `ScaleMode`/`Align`/`VAlign`) and round-trip as reflected
passthroughs in `ComponentSerializer` / `SceneSerializer`, with a row each in
`VKM_SCENE_COMPONENTS`. UI entities save and load with the scene like any other.
`UIImage` is an `R` row: its texture is written as the asset's name and named in
the scene's `assets` block, so the next load brings the picture with it. The
resolved fields - a list's measured sizes, a button's state and pointer - are
absent from those blocks on purpose: a scene file is what was authored. A
scroll view's `offset` is authored, and saved.

### Key files

- `src/engine/ecs/component/ui/ui_canvas.h` - `UICanvas` (layer root + reference-resolution scaling + sort order)
- `src/engine/ecs/component/ui/ui_element.h` - `UIRect` and `UIElement` (the 2D rect: anchor / pivot / position / size / visible / blocksPointer / clipChildren)
- `src/engine/ecs/component/ui/ui_image.h` - `UIImage` (filled quad)
- `src/engine/ecs/component/ui/ui_shape.h` - `UIShape` (corners, border and gradient, for `UIImage` and `UIButton` alike)
- `src/engine/ecs/component/ui/ui_text.h` - `UIText` (string + font name + size + colour + h/v alignment)
- `src/engine/ecs/component/ui/ui_button.h` - `UIButton` (per-state tints + event id)
- `src/engine/ecs/component/ui/ui_scroll.h` - `UIScroll` (offset + wheel step; measured content and view size)
- `src/engine/system/ui/ui_system.{h,cpp}` - `UISystem` (layout resolve + draw build + interaction resolve)
- `src/engine/system/ui/ui_draw_data.h` - `UIVertex` / `UIDrawCmd` / `UIDrawData` (the engine -> backend contract)
- `src/engine/system/ui/ui_events.h` - `UIClickEvent`
- `src/engine/system/ui/text_layout.h` - `walkGlyphs` / `measureText` (the one kerned walk along a line)
- `src/engine/core/utf8.h` - `Utf8::next` / `append` / `previous` (what UI text is decoded and edited by)
- `src/engine/platform/input/input_map.h` - `InputMap::text` / `typed` (what a text field reads)
- `src/engine/resource/asset/font_asset.h` - `FontAsset` (self-contained SDF atlas pixels + per-glyph metrics + kerning)
- `src/tools/loader/font_baker.{h,cpp}` - `bakeFontSDF` (stb_truetype -> SDF atlas and kerning table)
- `src/backend/opengl/pass/gl_ui_pass.{h,cpp}` - `GLUIPass` (the 2D overlay pass)
- `shaders/ui/` - the UI shader (Solid, flat or pictured / SDF-text in one program)
