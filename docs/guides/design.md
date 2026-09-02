# Design Guide

Where a change belongs, what shape it should take, and what "finished" means.
[engine.md](engine.md) says what you are building and inside which limits; this
says where your change goes within it. The bar for the code itself is
[implementation.md](implementation.md).

---

## Absolutes

- **A new ECS component is a plain data struct.** Bare members, no behavior.
  Logic lives in a system.
- **A system runs at exactly one `SystemStage`,** placed by responsibility, and
  relies on stage ordering rather than manual sequencing.
- **A system that produces a `FrameContext` product publishes it on every exit,**
  including the ones that produced nothing. Section 1.
- **A new asset kind is a `Resource` subclass** stored in `ResourceManager`,
  reached through a typed `Handle<T>`, and registered with the asset factories so
  serialization can cold-load it.
- **A render pass stays inside the backend.**
- **Every editor mutation goes through an undoable `Command`.** Section 2.5.
- **Docs ship with the code.** A change to what a subsystem *is* updates the
  matching page under [../reference/](../reference/) in the same commit.

---

## 1. The engine has a grain

Before writing anything new, find the closest existing sibling and match it. Not
because consistency is pleasant, but because predictability is the second thing
this engine optimises for: a reader who can guess the shape of the next file is a
reader who can work quickly and be right.

- A **system** subclasses `System`, implements `update(FrameContext&)`, and
  registers at a stage. It reads and writes through `FrameContext`, not globals.
  Three conventions the base class cannot enforce and every sibling follows:
  `update` opens with `PROFILE_SCOPE("<ClassName>")`, the deleted Rule of 5 is
  written out even with no state, and the class `@brief` says which stage it runs
  at and why that one
  ([code-style.md](code-style.md#136-a-system-subclass-spells-out-the-rule-of-5-it-inherits)).
- A **component** is data. `Transform` is the model: plain members plus *static*
  helpers, no instance methods that mutate.
- A **render pass** looks like the other passes and lives in the backend.
- An **asset kind** is a `Resource` subclass reached by handle.

If your new thing cannot be expressed in the same shape as its siblings, that is
a signal. Either you have misread the existing pattern, or you have a genuinely
new kind of problem - which is rare, and worth double-checking before assuming.

### 1.1 A producer publishes on every exit

If your system writes a pointer onto `FrameContext`, it owes that pointer on
**every** return path, including the ones where it did nothing. Clear the buffer
at the top of `update`, then work - not the other way round.
`system/visibility/visibility_system.cpp:139-140` states the reason in place:

    // Cleared here, not at the serial gather, so the early-return paths below
    // still publish an empty result instead of last frame's stale entries.

and `ctx.visibility = &m_result;` follows at all three exits (`:158`, `:191`,
`:271`). `ui_system.cpp:26-28` and `skeletal_animation_system.cpp:54-56` open the
same way.

A consumer must tell "ran, found nothing" from "never ran", and a null can only
say the second. Bailing out without publishing does not leave a consumer with
nothing - it leaves them last frame's answer, or a null they read as absence.

---

## 2. Three questions before you touch a file

Most changes are settled by the first or second.

### 2.1 Does this already exist?

Half-written abstractions are worse than none. If 80% of what you need is in
`HierarchyOperations`, the `tools/generator/` helpers or a culling stage, extend
that rather than starting a parallel utility. Search first - the engine has more
reusable machinery than is obvious: `SparseSet<T>`, `SlotAllocator`,
`parallelFor()` - a free function template in `Vkm::Engine`
(`platform/threading/thread_pool.h:138`), not a `ThreadPool` method, and every
call site spells it unqualified - the `core/reflect.h` field reflection, and the
event bus on `ctx.events`.

### 2.2 Where does it belong?

The directory tree encodes responsibility. Let it place your code:

| If the code is...                             | It belongs in...                     |
|-----------------------------------------------|--------------------------------------|
| Per-frame behavior over the scene             | `src/engine/system/<name>/`          |
| Pure data attached to an entity               | `src/engine/ecs/component/<subject>/` |
| A new asset *kind* - the `Resource` subclass itself | `src/engine/resource/asset/`   |
| Low-level container / handle / type machinery | `src/engine/core/memory/`            |
| Window, input, threading, dynamic library     | `src/engine/platform/<area>/`        |
| Profiling and error-reporting facades         | `src/engine/debug/`                  |
| Scene / asset / component (de)serialization   | `src/engine/io/`                     |
| What goes on the wire, or who decides what     | `src/engine/net/` - not a system; see [engine.md](engine.md#5-what-everything-else-stands-on) |
| GPU-specific work                             | `src/backend/opengl/`                |
| A shader                                      | `shaders/<pass>/`                    |
| Editor-only UI or interaction                 | `src/editor/` - and read [2.5](#25-four-editor-rules-that-fail-quietly) first |
| Importing or generating an asset              | `src/tools/loader/` or `src/tools/generator/` |
| Baking an asset into the form the runtime reads | `src/tools/cook/`                  |
| Registering a system, or anything every host does     | `app/engine_app.h`            |
| Gameplay                                      | `examples/<project>/src/` - never in the engine |

`src/engine/net/` is the row most likely to be read wrong, because it is the one
that is not a system. `NetSession` is owned by `Engine` by value like `Clock`
and `EventBus`, and it brackets the frame rather than taking part in it - so a
change about *when* the wire is read or written belongs in `Engine::run`, and a
system that wants to know who decides an entity asks `ctx.net.simulates(entity)`
rather than reaching for a reference. The socket itself is the platform row
above, in `platform/net/`.

Two of those are easy to miss because they are not under `src/engine/`. A new
asset kind splits: the type is a `Resource` subclass in `resource/asset/`, while
the code that *reads a file into it* is a loader in `src/tools/`. And a system is
declared in `system/<name>/` but comes alive at one line in `setupEngineApp`,
where every `addSystem<T>(stage)` call lives (`app/engine_app.h:89-110`) and
where the stage placement is argued in a comment beside it.

If it fits nowhere obvious, the tree is telling you the design is off. Stop and
reconsider rather than forcing it in.

### 2.3 What is the smallest change that fits?

A one-line method on an existing class beats a new helper file. A new enum value
beats a new parallel type. Prefer the change that adds the least new surface
while still being clean.

**Except where the thing you are adding to has already absorbed a case this
way,** and this is a real tension worth naming rather than glossing. This section
says take the smallest change; [review.md](review.md#12-a-field-per-case) holds
up `SceneIOController` - `m_playSnapshot`, then `m_playAssets`, then
`m_playSnapshotDirty`, then `m_playSnapshotHistory` - as the engine's canonical
anti-example. Every one of those four was, on the day it landed, exactly the
smallest change that fit. The file is now fourth in the engine for fixes.

Both rules are right; they answer different questions. This one ranks *how much
new surface* a change adds. Review's ranks *what the last four changes did to the
shape*. So the qualifier is a condition, not a re-ranking: **the smallest change
is the smallest one that does not add the fifth case.** When you are about to
add a field, a flag or a branch beside three that arrived the same way, the
smallest honest change is the one that removes the reason for cases - and per
[README.md](README.md#decide-or-ask), naming that and costing it is yours while
deciding to do it is not.

Outside that condition, 2.3 stands unqualified: least new surface wins.

### 2.4 A component that serializes

A plain struct is a component already: `scene.add<T>(e, ...)` is the whole
mechanism and nothing is registered. **Making it survive a save is a separate
step, and skipping it fails silently** - the component works for the whole
session and is simply not there the next time the scene loads. Nothing errors,
because from the serializer's side it was never there to write.

Three parts:

1. **Reflect it.** `VKM_REFLECT_BEGIN(::Vkm::Engine::T)` / `VKM_F(field)` /
   `VKM_REFLECT_END()`, at global scope after the header's namespace close
   ([code-style.md](code-style.md#21-reflected-types-close-the-namespace-first)).
2. **Give it a `save` / `load` pair** in `io/scene/component_serializer.{h,cpp}`,
   plus an `emitAssetRefs` overload beside them if it names an asset. For a
   reflected component the first two are one line each into the reflection
   driver.
3. **Add its row to `VKM_SCENE_COMPONENTS`**
   (`io/scene/component_serializer.h:75`), as a `P`, `R` or `E` row - the letter
   says what the component refers to, and decides what its save and load are
   handed (`:49-58`). Saving, loading, the known-key set and the scene's
   `assets` block all expand from that one list, which is why it is a list: a
   component saved but never loaded is silent round-trip loss that the
   unknown-key warning cannot catch, because the key is known.

Step 1 is the one with a boundary. 19 of the 26 components in the scene format
are reflected; the other seven are hand-written on purpose, for four reasons:

- **It references an asset by name.** `Mesh`, `LOD`, `Decal`, `AudioSource`,
  `Animator` - these are the `R` rows, whose save and load take the
  `ResourceManager` as well so a `Handle<T>` can be written as a name and
  resolved back on load, and which carry a third overload, `emitAssetRefs`,
  saying which assets the component names without writing them anywhere.
- **Its persisted surface is narrower than the struct.** `Animator` again: blend
  state is transient by design.
- **It holds something field iteration cannot reach.** `Animation`, whose
  `AnimationTrack<T>` keeps its keyframes private; `ScriptComponent`, which owns
  polymorphic behaviors.
- **It needs a second pass.** `Hierarchy` stores its parent as a scene-table
  index and is not a row at all - `saveComponents` writes it and the caller's
  pass 2 reads it, after the entity table exists.

Reflection and a hand-written `save` / `load` are not the same boundary, and
three rows sit on both sides of it. `Joint` and `Ragdoll` are the `E` rows:
reflected, and hand-written anyway, because an entity reference cannot survive a
file as the `EntityId` it is in memory - their save takes an `EntityNamer` and
their load an `EntityResolver`, and the reflected half still goes through the
driver in the first line of each (`component_serializer.cpp:165`, `:179`).
`Collider` is the third: it reflects `isTrigger` and `enabled` and writes its
parts array and mesh point cloud by hand (`:225`), which is field iteration's
reach again.

Getting *that* one wrong is loud, not silent: `Reflect::Traits<T>` is left
unspecialised deliberately, so calling the generic driver on an unreflected type
is a compile error telling you to add the markup.

`emitAssetRefs` is what keeps the scene's `assets` block on the list rather than
beside it. That block says which assets the file needs, and a reference it does
not name is one the next load resolves to nothing with nothing said at either
end, because the component's own key was written correctly. The walk that fills
it expands from the `R` rows (`io/asset/asset_serializer.cpp:290-296`), so an
`R` row with no overload is a compile error naming the component that needs one.
[../reference/system/io.md](../reference/system/io.md#adding-a-component-to-the-round-trip)
covers the same ground from the format's side.

**A component that replicates is a fourth step, and it fails the same silent
way.** Reflection, the serializer rows and the editor macros say nothing about
the wire: a component can round-trip a save perfectly and never cross a
connection. Give it `netEncode` / `netDecode` free functions beside it and
register it from the project's `vkmSetupNetwork`
([../reference/system/networking.md](../reference/system/networking.md#what-replicates)) -
from there and nowhere else, because the schema is rebuilt each time that entry
runs. Then ask the question that decides whether it should travel at all: is
this a cause only the authority knows, or an effect every end recomputes? An
effect on the wire is worse than wasteful - it arrives as a second writer for a
value that already had one.

None of this gives you authoring. The inspector card, the Create-menu entry and
the hierarchy badge are hand-written under `src/editor/`, and two more macro
lists decide what the editor can do with the component: absent from
`VKM_EDITOR_SNAPSHOT_COMPONENTS` (`editor/framework/editor_commands.h:317`) it is
one undo cannot resurrect, and absent from `VKM_EDITOR_COMMAND_COMPONENTS`
(`:358`) the inspector cannot add, remove or edit it undoably at all. Both are
documented where they live and in
[../reference/editor.md](../reference/editor.md#undo--redo).

### 2.5 Four editor rules that fail quietly

`src/editor/` is the one row in 2.2 where the local conventions are load-bearing
and none of them is enforced by the compiler. Read these before the first change:

- **Every mutation goes through an undoable `Command`**
  ([../reference/editor.md](../reference/editor.md#undo--redo)). Writing
  `scene.get<T>(e).field = v` compiles, works on screen, and silently breaks
  undo. On an entity inside a prefab instance it is a *different* command,
  `PrefabOverrideCommand`, because the value there is the prefab's patched by the
  instance's overrides.
- **Property rows go through the `prop*` wrappers, never raw ImGui.** 197 `prop*`
  calls in `src/editor` against 26 raw slider / drag / input / checkbox / colour
  calls, and those 26 are either not property rows or sit inside a `propRow`
  lambda - which is the wrapper, and is the escape hatch when no `prop*` fits the
  widget you need. The reason is in `ui/editor_widgets.h:60-70`: the wrappers pass
  `PROP_CLAMP`, which closes the Ctrl+click text-entry hole that otherwise makes
  every bound in the inspector advisory. Reaching past `propRow` too is what
  loses the bound.
- **Fixed metrics go through `EditorStyle::px(units)`**
  (`ui/editor_style.h:118`) - design pixels at the 15px reference font, scaled to
  the loaded font size. Over 140 call sites across `src/editor`. A raw pixel
  literal looks right on your display and wrong on a scaled one, and nothing
  catches it.
  The one exception is the ImGui style itself: `applyEditorTheme(scale)`
  (`ui/editor_theme.h:16`) is handed the window's content scale and passes it to
  `ScaleAllSizes`, because those metrics are set before there is a font to
  measure.
- **Dialogs use `beginDialog` / `dialogButtons` / `endDialog`**
  (`ui/editor_dialogs.h:18-35`). The scaffold owns the Escape-cancels /
  Enter-confirms contract, including the case where an active text field would
  otherwise swallow Enter in exactly the dialogs that need it.

### 2.6 A render pass

The engine's side of this is a refusal - passes are an OpenGL detail and stay in
the backend ([engine.md](engine.md#3-what-the-engine-refuses)) - so the whole
procedure is inside `src/backend/opengl/pass/`:

1. **Subclass `GLPass`** (`gl_pass.h`) and override `execute(GLFrameContext&)`.
   The base carries the shared fullscreen preamble / epilogue and the
   colour-chain promotion; use them rather than open-coding GL state.
2. **Gate yourself in the first lines of `execute()`.** 15 of the 19 passes do -
   `gl_bloom_pass.cpp:26` is `if (!ctx.view.settings.bloom) return;`,
   `gl_decal_pass.cpp:44` is `if (view.decals.empty()) return;`. The backend runs
   every pass unconditionally (`gl_backend.cpp:269-272`) and skips nothing,
   deliberately: only the pass knows what would make it a no-op, and the
   condition belongs with the knowledge.
3. **Register it in the ordered list** at `gl_backend.cpp:94-113`, with a name.
   The list is the schedule - there is no graph and no dependency declaration,
   and the comment above it says only that the order is load-bearing, pointing at
   [../reference/system/rendering.md](../reference/system/rendering.md#the-passes-fixed-order)
   for what each pass owes the next. Put yours where its inputs are already
   written, and say why there rather than in the list.
4. **Anything you produce for a later pass goes on `GLFrameContext`,** the
   backend's own per-frame product carrier. It is not `FrameContext`; the engine
   never sees it.
5. **Add no profile zone** - the loop already opened a CPU and a GPU one on your
   registered name ([code-style.md](code-style.md#101-profile_-macros)).

A new shader folder follows the loader's hardcoded filenames (`vertex.shader`,
`fragment.shader`, `computeShader.shader`) -
[../reference/building.md](../reference/building.md#shaders). What the existing
passes produce is
[../reference/system/rendering.md](../reference/system/rendering.md#the-passes-fixed-order),
worth reading first: several effects an agent reaches for are on
[engine.md](engine.md#4-what-has-already-been-decided)'s rejected list.

---

## 3. How much thinking does this deserve?

Deliberation scales with what a decision carries. The quality bar does not - a
leaf gets the same care in the code, just less argument about the design.

But **load accumulates while you are not looking.** Everything load-bearing in
this engine started as something small that other things then leaned on. So when
a thing is small, the useful question is not "how much does this carry today" but
**"what happens to everything else if this is wrong?"**

- Mistakes that stay local can be cheap.
- Anything naming a **format, an order, a lifetime, or an identity** will become
  a seam whether you intended it or not. Those get the full argument now.

---

## 4. Tracing the change

Once you know the shape, follow it outward. A change is rarely one file:

- Does this need a new `FrameContext` field, and at which stage is it written and
  read?
- Does a serializer in `io/` need to know about it?
- Does the editor need to author it - an inspector card, a menu entry, a panel?
- Does the cooker need a recipe?
- Does a reference doc now describe something that is no longer true?

---

## 5. What "finished" means

The code is most of the work and not all of it. A change is not done because it
compiles.

**It builds clean.** Zero first-party warnings.

```sh
cmake -B build -G Ninja        # an in-source build is refused, not warned about
cmake --build build
```

**You used it.** Not "the logic is correct" - you ran the thing and watched it
work. Press Play, Pause, Stop. Open the panel. Load the scene and save it. All
every host takes a project directory:

```sh
./build/bin/vkm_editor  examples/potion_runner   # edit a project
./build/bin/vkm_runtime examples/potion_runner   # play it
./build/bin/vkm_cook    examples/potion_runner   # bake its assets, no window
```

Walking the tool is still the half no suite covers. `ctest --test-dir build`
runs two suites now: `vkm_gl` from the submodule, and `vkm_engine_tests` -
headless assertions over the ECS, physics, queries, serialization and the
character controller, in `tests/vkm_engine_tests.cpp`. What those cannot see is
exactly what pressing Play sees: rendering, input, the editor, and every way the
pieces meet. A change is verified when both halves pass, not either one.

Where you cannot open a window, the exit codes are meant to be read:

```sh
timeout 10 ./build/bin/vkm_runtime examples/potion_runner
# 124 - still running when the timeout killed it, i.e. it booted
#   1 - it refused: no gameplay module, or no world of its own to open
```

Most defects found in this engine were found by someone using it, and almost none
by reading it. If a change has a visible result and you have not looked at it, it
is not finished, and if you could not look, say so plainly rather than implying
you did. Configure options, the hosts' project-resolution rule and the
shader conventions are in
[../reference/building.md](../reference/building.md).

**Docs ship with it.** A reference page describing last month's design is worse
than no page, because it is believed.

**It is committed in verified batches.** One coherent change per commit, built
and checked before it lands. Not one commit per file, and not everything held to
the end - a batch nobody can review is a batch nobody reviews.

**The message says why.** Lowercase `type(scope): summary` under 72 characters,
then prose at roughly 80 columns explaining why the change exists rather than
listing what it touched, which the diff already says. No line-initial `- `. No
`Co-Authored-By` and no self-attribution trailer.

**Never reference a task, version or commit in source comments.** That is the
commit's job, and a comment naming a release is stale the moment the next one
lands.

A change is finished when a reader cannot tell which lines are new from the style
alone, only from the feature they add.
