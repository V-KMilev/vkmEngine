# Design Guide

Where a change belongs, what shape it takes, and what "finished" means.
[engine.md](engine.md) says what you are building and inside which limits; the
bar for the code itself is [implementation.md](implementation.md).

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

Before writing anything new, find the closest sibling and match it.

- A **system** subclasses `System`, overrides `update` and/or `fixedUpdate`, and
  registers at a stage. It reads and writes through `FrameContext`, never
  globals. It opens its entry point with `PROFILE_SCOPE("<ClassName>")`, writes
  out the Rule of 5, and says in its `@brief` which stage it runs at and why
  ([code-style.md](code-style.md#71-rule-of-5---write-it-out)).
- A **component** is data. `Transform` is the model: plain members, *static*
  helpers, no mutating instance methods.
- A **render pass** looks like the other passes and lives in the backend.
- An **asset kind** is a `Resource` subclass reached by handle.

A new thing that cannot take its siblings' shape usually means the pattern was
misread; a genuinely new kind of problem is rare.

### 1.1 A producer publishes on every exit

A system that writes a pointer onto `FrameContext` owes it on **every** return
path: clear the buffer at the top of `update`, then work
(`system/visibility/visibility_system.cpp`). Bailing out without publishing
leaves consumers last frame's answer.

---

## 2. Three questions before you touch a file

### 2.1 Does this already exist?

If most of what you need exists, extend it. The reusable machinery:
`HierarchyOperations`, the `resource/generate/` helpers, `SparseSet<T>`,
`SlotAllocator`, `TypeRegistry<Base>` (one slot per type), `parallelFor()`
(`platform/threading/thread_pool.h`), the field reflection in `core/reflect.h`,
and the event bus on `ctx.events`.

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
| Registering a system                          | `app/engine_app.h`                   |
| The prologue every host runs - root, log, module, the project's world | `src/tools/project_boot.h` |
| Gameplay                                      | `examples/<project>/src/` - never in the engine |
| A test of engine code                         | `tests/<area>/<suite>_tests.cpp`, the area named for the source folder it tests; `tests/render/` is the GPU binary |

`NetSession` is owned by `Engine` and brackets the frame: a change about *when*
the wire is read or written belongs in `Engine::run`, and a system asking who
decides an entity calls `ctx.net.simulates(ctx.scene, entity)`.

A new system comes alive at one `addSystem<T>(stage)` line in `setupEngineApp`
(`app/engine_app.h`), its stage argued beside it. If code fits nowhere obvious,
the design is off.

**How a folder grows.** A folder's root holds its entry point and what every
part of it shares; a feature gets a subfolder named for it (`system/physics/`,
`net/`) only when the folder needs one - more than a screenful of files, or two
features someone would look for by name. Many members of one kind, named for
that kind (`pass/gl_*_pass`, `panels/*_panel`), stay flat: the name pattern is
the index. Nothing is named `common`, `misc`, `utils` or `helpers`: shared code
sits at the root of the narrowest folder that shares it. Tests follow the
source.

### 2.3 What is the smallest change that fits?

A one-line method on an existing class beats a new helper file; a new enum value
beats a parallel type.

But it is **the smallest change that does not add the fifth case**
([review.md](review.md#12-a-field-per-case)). About to add a field, flag or
branch beside three that arrived the same way, name and cost the change that
removes the reason for cases; deciding to make it is the owner's
([README.md](README.md#decide-or-ask)).

### 2.4 A component that serializes

Any plain struct is a component: `scene.add<T>(e, ...)`. **Surviving a save is
a separate step, and skipping it fails silently** - the component works all
session and is absent next load.

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
   entities.

Where a component needs more than the driver, see
[../reference/io.md](../reference/io.md#adding-a-component-to-the-round-trip).

**Replicating is a fourth step and fails the same silent way**: `netEncode` /
`netDecode` beside it, registered from the project's `vkmSetupNetwork`
([networking.md](../reference/networking.md#what-replicates)). Replicate a cause
only the authority knows, not an effect every end recomputes - an effect on the
wire is a second writer.

**Authoring is separate again.** The inspector card, the Create-menu entry and
the hierarchy badge are hand-written in `src/editor/`, and the component must be
in `VKM_EDITOR_COMPONENTS` (`editor/command/editor_commands.h`) or undo cannot
restore it and the inspector cannot edit it undoably
([../reference/editor.md](../reference/editor.md#undo--redo)).

### 2.5 Four editor rules that fail quietly

The compiler enforces none of these:

- **Every mutation goes through an undoable `Command`.** Writing
  `scene.get<T>(e).field = v` compiles, works on screen, and breaks undo.
  Reach for `EditScope<T>` (`command/component_edit.h`) by default; it pushes the
  right step when it closes, prefab overrides included. `pushEdit` / `editStep`
  are for a gesture spanning frames (a drag, the gizmo).
- **Property rows go through the `prop*` wrappers, never raw ImGui.** They pass
  `PROP_CLAMP`, without which Ctrl+click text entry ignores every inspector
  bound. `propRow` is the escape hatch when no `prop*` fits.
- **Fixed metrics go through `EditorStyle::px(units)`** (`ui/editor_style.h`) -
  design pixels scaled to the loaded font; a raw pixel literal is wrong on a
  scaled display. The exception is the ImGui style, which
  `applyEditorTheme(scale)` sets before there is a font. A scaled number two
  files must agree on is a named nullary function in `ui/editor_style.h`
  (`overlayButton()`).
- **Dialogs use `beginDialog` / `dialogButtons` / `endDialog`**
  (`ui/editor_dialogs.h`), which own Escape-cancels and Enter-confirms.

### 2.6 A render pass

Passes are a backend detail and stay in `src/backend/opengl/`
([engine.md](engine.md#3-what-the-engine-refuses)):

1. **Subclass `GLPass`** (`gl_pass.h`) and override `execute(GLFrameContext&)`.
   The backend resets depth, blending and culling before every pass; there is
   no epilogue to write.
2. **Gate yourself in the first lines of `execute()`** - `if
   (!ctx.view.settings.bloom) return;`. The backend runs every pass.
3. **Register it in the ordered list** in `gl_backend.cpp`, after the passes that
   write its inputs. The list is the schedule - no graph
   ([rendering.md](../reference/rendering.md#the-passes-fixed-order)).
4. **What you produce for a later pass goes on `GLFrameContext`,** the backend's
   own per-frame carrier. The engine never sees it.
5. **Open no top-level profile zone** - the loop already opens one on your
   registered name ([code-style.md](code-style.md#9-logging-and-profiling)).
6. **Count what you bind and draw**, which is what a pass costs
   ([implementation.md](implementation.md#2-know-how-often-it-runs)). A mip chain
   is walked in compute, a dispatch a level. Scene geometry goes out through a
   `GLDrawList` as one multi-draw per change of program, material or vertex
   layout, never a draw per mesh.
7. **Per-frame data goes through the frame's stream** (`ctx.resources.stream()`,
   a `Vkm::GL::StreamRing`), not a buffer of the pass's own refilled each frame;
   a buffer the GPU writes stays its owner's. GL objects are edited by name
   as vkmGL does - a pass binds only to draw or sample.
8. **Depth has one convention, reversed for the eye.** Clear, test and read it
   through `convention/gl_depth.h` and `shaders/depth.glsl` (`EYE_CLEAR`,
   `EYE_TEST`, `isFarDepth`, `nearerDepth`...), never a literal `1.0` or
   `GL_LESS`; a shadow map keeps near 0, far 1 through `GLDepth::shadowProjection`.

A new shader folder uses the loader's filenames
([building.md](../reference/building.md#shaders)). Check
[engine.md](engine.md#4-what-has-already-been-decided)'s rejected list before
adding an effect.

---

## 3. How much thinking does this deserve?

Deliberation scales with what a decision carries; the quality bar does not.
Ask of a small thing **"what happens to everything else if this is wrong?"**
Anything naming a **format, an order, a lifetime or an identity** becomes a seam
whether you meant it to or not, and gets the full argument now.

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
`ctest --test-dir build`, every suite
([../reference/building.md](../reference/building.md#tests)). A bug fix comes
with a test you have seen fail without the fix.

**You used it** - ran it and watched it work. Press Play, Pause, Stop; open the
panel; load the scene and save it; look at the frame. Without a window, read the
runtime's exit code
([../reference/building.md](../reference/building.md#build-commands)). A visible
result you have not looked at is not finished; if you could not look, say so.

**A performance change carries its measurement,** before and after, on the
scenes it touches ([building.md](../reference/building.md#measuring-a-change)).

**Docs ship with it.** A stale reference page is believed.

**It is committed in verified batches** - one coherent change per commit, built
and checked.

**The message says why, briefly.** Lowercase `type(scope): summary` under 72
characters, then a few lines of prose at about 80 columns: what the change does
as a whole, and why - not a tour of what it touched. A commit that folds others
in sums up its subject; it does not list what was folded. No line-initial `- `,
no `Co-Authored-By`, no self-attribution trailer.

A change is finished when a reader cannot tell which lines are new from the style
alone, only from the feature they add.

