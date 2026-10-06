# Design Guide

Where a change belongs, what shape it takes, and what "finished" means.
[engine.md](engine.md) says what you are building and inside which limits; this
says where your change goes within it. The bar for the code itself is
[implementation.md](implementation.md).

---

## Absolutes

- **A new ECS component is a plain data struct.** Bare members, no behavior.
  Logic lives in a system.
- **A system runs at exactly one `SystemStage`,** placed by responsibility, and
  relies on stage order rather than manual sequencing.
- **A system that produces a `FrameContext` product publishes it on every exit,**
  including the ones that produced nothing (1.1).
- **A new asset kind is a `Resource` subclass** stored in `ResourceManager`,
  reached through a typed `Handle<T>`, and registered with the asset factories so
  serialization can cold-load it.
- **Every editor mutation goes through an undoable `Command`** (2.5).
- **Docs ship with the code.** A change to what a subsystem *is* updates its page
  under [../reference/](../reference/) in the same commit.

---

## 1. The engine has a grain

Before writing anything new, find the closest sibling and match it. Not because
consistency is pleasant, but because predictability is the second thing the
engine optimises for: a reader who can guess the shape of the next file works
quickly and is right.

- A **system** subclasses `System`, overrides `update` and/or `fixedUpdate`, and
  registers at a stage. It reads and writes through `FrameContext`, never
  globals. Three conventions the base class cannot enforce: the entry point opens
  with `PROFILE_SCOPE("<ClassName>")`, the Rule of 5 is written out even with no
  state, and the class `@brief` says which stage it runs at and why that one
  ([code-style.md](code-style.md#71-rule-of-5---write-it-out)).
- A **component** is data. `Transform` is the model: plain members, *static*
  helpers, no mutating instance methods.
- A **render pass** looks like the other passes and lives in the backend.
- An **asset kind** is a `Resource` subclass reached by handle.

A new thing that cannot take the shape of its siblings is a signal: you have
misread the pattern, or you have a genuinely new kind of problem. The second is
rare; check before assuming it.

### 1.1 A producer publishes on every exit

A system that writes a pointer onto `FrameContext` owes that pointer on
**every** return path, including the ones where it did nothing. Clear the buffer
at the top of `update`, then work - not the other way round.
`system/visibility/visibility_system.cpp` publishes `ctx.visibility` at every
exit, early returns included, and says why:

    // Cleared here, not at the serial gather, so the early-return paths below
    // still publish an empty result instead of last frame's stale entries.

A consumer must tell "ran, found nothing" from "never ran", and a null can only
say the second. Bailing out without publishing leaves them last frame's answer.

---

## 2. Three questions before you touch a file

### 2.1 Does this already exist?

A half-written abstraction is worse than none. If most of what you need is in
`HierarchyOperations`, the `resource/generate/` helpers or a culling stage,
extend that. The engine has more reusable machinery than is obvious:
`SparseSet<T>`, `SlotAllocator`, `TypeRegistry<Base>` (one slot per type - the
`Scene`, the `ResourceManager` and the `EventBus` are built on it), `parallelFor()`
(a free function in `platform/threading/thread_pool.h`, not a `ThreadPool`
method), the field reflection in `core/reflect.h`, and the event bus on
`ctx.events`.

### 2.2 Where does it belong?

The directory tree encodes responsibility. Let it place your code:

| If the code is...                             | It belongs in...                     |
|-----------------------------------------------|--------------------------------------|
| Per-frame behavior over the scene             | `src/engine/system/<name>/`          |
| Pure data attached to an entity               | `src/engine/ecs/component/<subject>/` |
| An operation on the entity graph many systems share (the hierarchy) | `src/engine/ecs/` |
| A new asset *kind* - the `Resource` subclass  | `src/engine/resource/asset/`         |
| Low-level container / handle / type machinery | `src/engine/core/memory/`            |
| Window, input, threading, dynamic library     | `src/engine/platform/<area>/`        |
| Profiling and error-reporting facades         | `src/engine/debug/`                  |
| Scene / asset / component (de)serialization   | `src/engine/io/`                     |
| What goes on the wire, or who decides what    | `src/engine/net/` - not a system; see [engine.md](engine.md#5-what-everything-else-stands-on) |
| GPU-specific work                             | `src/backend/opengl/`                |
| A shader                                      | `shaders/<pass>/`                    |
| Editor-only UI or interaction                 | `src/editor/` - read [2.5](#25-four-editor-rules-that-fail-quietly) first |
| Importing an asset from its source art (cook-only) | `src/tools/import/`              |
| Loading what the runtime reads, or generating an asset | `src/tools/loader/` or `src/engine/resource/generate/` |
| Baking an asset into the form the runtime reads | `src/tools/cook/`                  |
| Registering a system: the stack every host stands up | `app/engine_app.h`               |
| The prologue every host runs - root, working directory, log, module, and the project's world (tick rate, entry scene, its fingerprint) | `src/tools/project_boot.h` |
| Gameplay                                      | `examples/<project>/src/` - never in the engine |
| A test of engine code                         | `tests/<area>/<suite>_tests.cpp`, the area named for the source folder it tests; `tests/render/` is the GPU binary |

`src/engine/net/` is the row most often read wrong, because it is not a system:
`NetSession` is owned by `Engine` by value and brackets the frame. A change about
*when* the wire is read or written belongs in `Engine::run`; a system that wants
to know who decides an entity asks `ctx.net.simulates(entity)`. The socket is
`platform/net/`.

Two rows are easy to miss because they leave `src/engine/`: a new asset kind is
a type in `resource/asset/` plus a loader in `src/tools/` that reads a file into
it, and a new system comes alive at one `addSystem<T>(stage)` line in
`setupEngineApp` (`app/engine_app.h`), where its stage is argued beside it.

If it fits nowhere obvious, the tree is telling you the design is off.

**How a folder grows.** A folder's root holds its entry point and what every
part of it shares; each feature it grows gets a subfolder named for that
feature. `system/physics/` (`physics_system` at the root; `collision/`,
`solver/`, `query/`, `character/`, `authoring/`) and `net/` (`net_session`;
`wire/`, `transport/`, `replication/`, `prediction/`) are the model. A folder
gets feature subfolders only when it needs them - more than a screenful of
files, or two features someone would look for by name. A folder of many members
of one kind, each named for that kind (`pass/gl_*_pass`, `panels/*_panel`),
stays flat, because the name pattern is its index. Nothing is named `common`,
`misc`, `utils` or `helpers`: shared code sits at the root of the narrowest
folder that shares it, named for what it is. Tests follow the source.

### 2.3 What is the smallest change that fits?

A one-line method on an existing class beats a new helper file; a new enum value
beats a parallel type. Prefer the change that adds the least new surface while
staying clean.

**Except where the thing you are adding to has already absorbed cases this
way.** `SceneIOController` recorded a play session as `m_playSnapshot`, then
`m_playAssets`, then `m_playSnapshotDirty`, then `m_playSnapshotHistory` - each,
on the day it landed, the smallest change that fit, and together the reason the
file kept needing fixes ([review.md](review.md#12-a-field-per-case)). They are
one `PlaySnapshot` now, and that change was larger than any of the four.

So the smallest change is **the smallest one that does not add the fifth case.**
When you are about to add a field, flag or branch beside three that arrived the
same way, the honest change removes the reason for cases - and naming and
costing it is yours, while deciding to do it is the owner's
([README.md](README.md#decide-or-ask)).

### 2.4 A component that serializes

Any plain struct is a component: `scene.add<T>(e, ...)` is the whole mechanism.
**Surviving a save is a separate step, and skipping it fails silently** - the
component works all session and is simply absent next load.

1. **Reflect it** - `VKM_REFLECT_BEGIN(::Vkm::Engine::T)` / `VKM_F(field)` /
   `VKM_REFLECT_END()` at global scope
   ([code-style.md](code-style.md#21-reflected-types-close-the-namespace-first)).
   Reflecting only the fields that are authored is how a component says the rest
   is session state.
2. **Give it a `save` / `load` pair** in `io/scene/component_serializer.{h,cpp}` -
   one line each into the reflection driver - plus an `emitAssetRefs` overload if
   it names an asset.
3. **Add its row to `VKM_SCENE_COMPONENTS`** (`io/scene/component_serializer.h`)
   as `P`, `R` or `E`, by what it refers to: plain data, assets by name, or other
   entities. Saving, loading, the known-key set and the scene's `assets` block all
   expand from that one list.

Where a component needs more than the driver - an entity reference, private
state, polymorphic behaviours - and why, is
[../reference/io.md](../reference/io.md#adding-a-component-to-the-round-trip).

**Replicating is a fourth step and fails the same silent way.** Give it
`netEncode` / `netDecode` beside it and register it from the project's
`vkmSetupNetwork` ([networking.md](../reference/networking.md#what-replicates)).
Then ask whether it should travel at all: is this a cause only the authority
knows, or an effect every end recomputes? An effect on the wire is a second
writer for a value that already had one.

**Authoring is separate again.** The inspector card, the Create-menu entry and
the hierarchy badge are hand-written in `src/editor/`, and the component must be
in `VKM_EDITOR_COMPONENTS` (`editor/command/editor_commands.h`) or undo cannot
restore it and the inspector cannot edit it undoably
([../reference/editor.md](../reference/editor.md#undo--redo)).

### 2.5 Four editor rules that fail quietly

`src/editor/` is where the local conventions are load-bearing and the compiler
enforces none of them:

- **Every mutation goes through an undoable `Command`.** Writing
  `scene.get<T>(e).field = v` compiles, works on screen, and breaks undo; inside
  a prefab instance it is a different command again, because the value there is
  the prefab's patched by overrides. `EditScope<T>` (`command/component_edit.h`)
  holds the component open and pushes the right step when it closes - reach for
  it by default. `pushEdit` / `editStep` are for a gesture spanning frames (a
  drag, a scrub, the gizmo), which marks the scene dirty as it goes and pushes
  one step at the end.
- **Property rows go through the `prop*` wrappers, never raw ImGui.** The
  wrappers pass `PROP_CLAMP`, which closes the Ctrl+click text entry that
  otherwise makes every inspector bound advisory. `propRow` is the escape hatch
  when no `prop*` fits; reaching past it too is what loses the bound.
- **Fixed metrics go through `EditorStyle::px(units)`** (`ui/editor_style.h`) -
  design pixels scaled to the loaded font. A raw pixel literal looks right on
  your display and wrong on a scaled one. The ImGui style itself is the one
  exception: `applyEditorTheme(scale)` sets it before there is a font to measure.
  A scaled number cannot be `constexpr`, so one that two files must agree on is a
  named nullary function in `ui/editor_style.h` (`overlayButton()`,
  `overlayPad()`); a number one place uses stays `px(8.0f)` at that place.
- **Dialogs use `beginDialog` / `dialogButtons` / `endDialog`**
  (`ui/editor_dialogs.h`), which own Escape-cancels and Enter-confirms, including
  when a text field would otherwise swallow Enter.

### 2.6 A render pass

Passes are an OpenGL detail and stay in the backend
([engine.md](engine.md#3-what-the-engine-refuses)), so the whole procedure is in
`src/backend/opengl/`:

1. **Subclass `GLPass`** (`gl_pass.h`) and override `execute(GLFrameContext&)`.
   The backend resets depth, blending and culling before every pass, so nothing
   a pass changes outlives it and there is no epilogue to write.
2. **Gate yourself in the first lines of `execute()`** - `if
   (!ctx.view.settings.bloom) return;`. The backend runs every pass; only the
   pass knows what makes it a no-op.
3. **Register it in the ordered list** in `gl_backend.cpp`. The list is the
   schedule - no graph, no dependency declaration. Put yours where its inputs are
   written; what each pass owes the next is
   [rendering.md](../reference/rendering.md#the-passes-fixed-order).
4. **What you produce for a later pass goes on `GLFrameContext`,** the backend's
   own per-frame carrier. The engine never sees it.
5. **Open no top-level profile zone** - the loop already opens a CPU and a GPU
   one on your registered name; sub-zones for phases inside are welcome
   ([code-style.md](code-style.md#9-logging-and-profiling)).
6. **Count what you bind and draw**, which is what a pass costs
   ([implementation.md](implementation.md#2-know-how-often-it-runs)). A mip chain
   is walked in compute - a dispatch a level, writing it as an image - which is
   how bloom, GTAO's depth and the reflection chain each cost the CPU half of what
   their draws did. Scene geometry goes out through a `GLDrawList` - a batch's,
   or the pass's own - as one multi-draw per change of program or material,
   never a draw per mesh.

A new shader folder uses the loader's filenames (`vertex.shader`,
`fragment.shader`, `compute.shader` -
[building.md](../reference/building.md#shaders)). Several effects an agent
reaches for are on [engine.md](engine.md#4-what-has-already-been-decided)'s
rejected list.

---

## 3. How much thinking does this deserve?

Deliberation scales with what a decision carries; the quality bar does not.

But **load accumulates while you are not looking.** Everything load-bearing here
started as something small that other things then leaned on. So the question for
a small thing is not "how much does this carry today" but **"what happens to
everything else if this is wrong?"** Anything naming a **format, an order, a
lifetime or an identity** becomes a seam whether you meant it to or not; those
get the full argument now.

---

## 4. Tracing the change

A change is rarely one file. Follow it outward:

- Does it need a `FrameContext` product, and at which stage is it written and
  read?
- Does a serializer in `io/` need to know?
- Does the editor need to author it?
- Does the cooker need a recipe?
- Does a reference page now describe something that is no longer true?

---

## 5. What "finished" means

**It builds clean,** with zero first-party warnings, and **the suites pass** -
`ctest --test-dir build`, all three
([../reference/building.md](../reference/building.md#tests)). A bug fix comes
with a test you have seen fail without the fix.

**You used it.** Not "the logic is correct" - you ran it and watched it work.
Press Play, Pause, Stop; open the panel; load the scene and save it; look at the
frame. Every host takes a project directory, and where you cannot open a window
the runtime's exit code is meant to be read
([../reference/building.md](../reference/building.md#build-commands)).

Most defects in this engine were found by someone using it. If a change has a
visible result and you have not looked at it, it is not finished - and if you
could not look, say so.

**A performance change carries its measurement,** before and after, on the
scenes it touches ([building.md](../reference/building.md#measuring-a-change)).

**Docs ship with it.** A reference page describing last month's design is worse
than none, because it is believed.

**It is committed in verified batches.** One coherent change per commit, built
and checked before it lands.

**The message says why, briefly.** Lowercase `type(scope): summary` under 72
characters, then a few lines of prose at about 80 columns: what the change does
as a whole, and why. Not a tour of what it touched - the diff already says that,
and a reader who wants the detail reads it. A commit that folds others in sums
up its subject; it does not list what was folded. No line-initial `- `, no
`Co-Authored-By`, no self-attribution trailer.

A change is finished when a reader cannot tell which lines are new from the style
alone, only from the feature they add.
