# The Engine

What vkmEngine is, what it values when two good things conflict, and what has
already been settled. This guide outranks the others: they describe how to build
well, and this one describes what you are building and inside which limits.

---

## Absolutes

Nothing here is a matter of judgment. Breaking one of these is a design problem
to raise, not a tradeoff to make.

- **Engine code never includes a `gl_*` header.** The GPU is reached only
  through the abstract interfaces in `system/render/` - `RenderBackend` for the
  frame, `EditorRenderHooks` for authoring-only work (section 5).
- **`Scene` is never edited to "support" a component.** Any plain struct is a
  component already; `scene.add<T>(entity, ...)` is the whole mechanism.
- **Components hold `Handle<T>`, never asset pointers or copies.** Serialization
  stores asset *names*, resolved back to handles on load.
- **Systems never call each other.** `FrameContext` is the only per-frame channel
  between them. `EditorSystem` is the one sanctioned exception and is not a
  precedent - section 5.
- **A serialized or cooked format never grows a back-compatibility read path.**
  No migration step, no `if (version == 1)` branch, no two live readers. What
  each format's version gate does about an old file is section 5.1 - it is not
  the same answer in all four.
- **Windows and Linux only.** No macOS branches, no macOS in the docs.
- **Nothing in [section 4](#4-what-has-already-been-decided) is reopened inside a
  task.** Reopening a settled decision is a conversation with the owner.

---

## 1. What this is

A C++17 engine with an OpenGL 4.3 backend, for Windows and Linux, that **runs
projects**. It holds no game of its own. A game is a directory - a
`project.json`, its scenes and assets, and gameplay code built into its own
module - and the same three executables (`vkm_editor`, `vkm_runtime`,
`vkm_cook`) run any of them. Each finds a project by the same rule: *the project
beside the executable, unless an argument names a different one.*

That separation is the engine's central idea, and most of the absolutes above
exist to protect it.

It is built to be **finished rather than rewritten**. Success is not how many
features ship this year; it is whether the thing still holds up in three years
without a subsystem being torn out. Every trade below follows from that sentence.

---

## 2. What it optimises for

Most decisions are easy because only one thing is at stake. This is the order for
when two good things genuinely conflict, and it is the tiebreaker the other
guides defer to:

1. **Correctness that survives load.** A design that stays right as things pile
   onto it beats one that is right today. Wins over everything below.
2. **Predictability.** A reader should be able to guess the shape of the next
   file. A change that invents its own convention costs more than it saves even
   when it is locally better.
3. **Simplicity.** Fewer moving parts, fewer concepts, less surface. But
   *simple*, not merely *small*: if the explicit version is the one that stays
   correct under load, it beats the compact one.
4. **Measured performance.** Data layout over cleverness. The frame is GPU-bound,
   so a CPU-side optimisation is not a win until a profile says so.
5. **Features.** Last, deliberately. A feature that compromises the four above is
   one the engine pays for every year it exists.

When you cannot decide, ask which choice you would rather be living with in three
years. That resolves most of them.

---

## 3. What the engine refuses

Positions, not preferences. Each with its reason, so "why don't we just..." has
an answer:

- **No back-compatibility for old formats.** Migration paths are permanent weight
  carried for a transitional problem, and two live formats mean every future
  reader must know both. The cost lands elsewhere: a format has to be right the
  first time, so scrutinise a format change harder than almost anything else.
- **No temporal antialiasing, ever.** Not "not yet". Techniques needing a
  temporal filter are therefore off the table too, and one has already been
  removed for it (section 4).
- **No speculative abstraction.** No flag, virtual or interface without a
  concrete second user you can name today.
- **No engine-level render pass abstraction.** Passes are an OpenGL
  implementation detail and stay inside the backend. The engine's view of
  rendering is `RenderBackend`, the backend-agnostic `RenderView` it is handed,
  and `EditorRenderHooks` (section 5). Nothing else, and no fourth one without
  the owner.
- **No bulk recovery of pre-refactor code.** When a subsystem is rewritten, the
  old sources are not mined for the missing feature; it is written fresh against
  the new shape. Recovered code carries the assumptions of the design it came
  from.
- **No macOS.**

---

## 4. What has already been decided

So that nobody spends a day re-proposing something settled. Each entry is the
outcome and the reason.

| Decided                  | Outcome and reason                                    |
|--------------------------|-------------------------------------------------------|
| Temporal AA              | Rejected permanently. AA is MSAA plus `specularAA()` in the PBR shader. |
| Contact shadows          | Removed in 1.3. Needs a temporal filter, which is off the table; GTAO covers its role. |
| Screen-space reflections | Removed. Reflection probes serve the case.            |
| SDFGI / voxel GI         | Never built, not planned. Indirect diffuse is a baked SH-L1 irradiance volume plus reflection probes plus GTAO. |
| Auto-exposure            | Proposed and rejected outright.                       |
| Shader permutations      | Built, measured, reverted. No win on a GPU-bound frame. |
| Draw-sort hoisting       | Built, measured, reverted. Same reason.               |
| Buffer-stall fixes       | Built, measured, reverted. Same reason.               |

The pattern in the last three is the lesson: **the frame is GPU-bound**, so a
CPU-side optimisation needs a profile before it needs a patch. Assume any obvious
render idea has been tried, and read
[../reference/system/rendering.md](../reference/system/rendering.md) before
proposing one.

---

## 5. What everything else stands on

These carry the rest of the engine, so a mistake in one is never local. The
absolutes at the top state the rules; this is which pieces they protect:

- **`Scene` and the ECS** - open, type-erased, no registration.
- **`FrameContext`** - references are services, pointers are per-frame stage
  products.
- **`Handle<T>` and `ResourceManager`** - assets owned once, referenced by
  handle, serialized by name.
- **The scene, prefab, project and cooked asset formats** - see the clean-break
  rule above and 5.1 below.
- **`RenderBackend`** - the frame-path seam between engine and GPU. There is a
  second, deliberately: `EditorRenderHooks`
  (`system/render/editor_render_hooks.h:69`) is offscreen rendering for authoring
  tools - asset thumbnails, material previews - which a shipped game never asks
  for. `GLBackend` inherits both (`backend/opengl/gl_backend.h:61`) and the
  editor asks for it by `dynamic_cast` through `editorRenderHooks()`
  (`editor_render_hooks.h:125`), showing placeholders when the answer is null.
  It lives under `system/render` rather than in the editor because the backend
  has to implement it and cannot see editor code. Two seams, both named here; an
  authoring-side GPU need extends this one rather than inventing a third.
- **Stage order** - Input, Simulation, Transform, Visibility, Render, UI. A
  system is placed by responsibility and relies on ordering, never on manual
  sequencing.

`EditorSystem` is the one system that holds references to others - the five in
the `AppSystems` bundle (camera, UI, visibility, render, audio) plus the
`ScriptModule` that owns the gameplay library, taken by constructor
(`editor/editor_system.h:54-63`) and wired at
`app/editor/main.cpp:36-38`. That is allowed because it is not participating in
the frame's data flow: it is the authoring tool *driving* the engine, at the UI
stage, after every producer has already run. Nothing else gets to do this. A
system that finds itself wanting a reference to another system has a
`FrameContext` product it has not defined yet.

### 5.1 What each format's version gate actually does

The rule above is one rule; the enforcement is four different mechanisms, and an
agent bumping a constant needs to know which it is holding.

| Format         | Constant                                              | What the reader refuses                       |
|----------------|-------------------------------------------------------|-----------------------------------------------|
| Cooked asset   | `io/asset/asset_cook.h:40-44` - five, one per asset kind | Anything but an exact match. A stale artifact is refused and re-cooked. |
| Scene          | `io/scene/scene_serializer.cpp:42` (`= 2`)            | Missing, zero, or *newer* than this build (`:314-323`). Nothing else. |
| Prefab         | `io/scene/prefab.cpp:35` (`= 3`)                      | The same shape (`:94-99`).                    |
| `project.json` | `io/project.cpp:54-60` `engineVersion`                | Nothing. It warns, deliberately - "refusing to open would be worse". It is provenance, not a format version. |

The consequence for scene and prefab: **bumping the constant does not by itself
stop an old file loading.** The gate refuses the future, not the past; an older
file is read by the current reader and a removed field simply comes back as its
default. So say in the commit what you expect an old file to do. If the answer
needs to be "refuse it", that is a change to what the gate does and belongs with
the owner, not inside the task.

And when you change one, there is nothing in-tree to round-trip against: **the
repo ships no scene and no prefab.** Both `examples/*/scenes/` are empty and both
examples build their worlds in code, so testing a format change means authoring a
scene by hand in the editor first, then saving and reloading it.

---

## 6. Facts that have cost real time

Not rules. Things that are true, are not obvious, and have each produced a wrong
result somebody had to chase down.

- **Forward is +Z.** GLM's `quatLookAt` aims 180 degrees off it. A sun overhead
  needs a *positive* pitch. Silently wrong-looking output, never an error.
- **The frame is GPU-bound.** See section 4.
- **Screen-right is -X** in the engine's right-handed basis.
