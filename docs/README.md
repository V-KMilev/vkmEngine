# docs - agent operating manual

This folder is the operating manual for anyone (human or AI agent) working in
vkmEngine. It exists so you start from the project's actual rules, mental model,
and structure instead of guessing from whatever file you happen to open first.

It has two halves:

- **`guides/`** - how we work: the rules and judgment that apply to every change.
  Start at [getting-started.md](guides/getting-started.md) if you are building a
  game *with* the engine rather than working *on* it.
- **`reference/`** - how the engine works: a deep-dive per subsystem.

> **The code is the source of truth.** These docs are kept current, but code
> changes faster than prose. If a doc disagrees with the code, trust the code and
> fix the doc (or flag the drift). Every reference doc was last verified against
> source - keep that contract.

---

## Read this before touching code

Do these in order. The first four are short and apply to *every* task; the fifth
is whichever subsystem you're about to change.

1. **Orient** - [reference/project-overview.md](reference/project-overview.md).
   One page: what the engine is, the core model, system order, the rendering and
   resource summary.
2. **What you are building** - [guides/engine.md](guides/engine.md). What the
   engine is for, the order it trades in when two good things conflict, and the
   questions already settled so nobody re-proposes them.
3. **How to fit the change** - [guides/design.md](guides/design.md). The engine
   has a grain; this is how to find it, where code belongs, which seams you must
   not cross, and what "finished" means.
4. **The quality bar** - [guides/implementation.md](guides/implementation.md).
   Simplest thing that solves today's problem; generic enough to not bite later
   but no more; clean and readable.
5. **The mechanics** - [guides/code-style.md](guides/code-style.md). Naming,
   layout, comments, formatting, class anatomy, includes - so your diff reads like
   it was always there.
6. **Judging what is already there** - [guides/review.md](guides/review.md). The
   other three assume you are adding something; this one asks whether the shape
   the engine already has is the right one, and how to tell when fixes have
   started standing in for a design.
7. **The subsystem you're touching** - the matching doc under
   [reference/](reference/) (architecture, ecs, resources, threading, editor, or
   `reference/system/` for rendering, lighting, visibility, hierarchy, animation,
   events, io, scripting, physics, ui, audio).

If you only have time for one thing before a small change: skim the relevant
reference doc and the development guide.

---

## The working loop

For any non-trivial task:

1. **Understand first.** Read the subsystem doc and the actual source. Know the
   data flow - who writes a component, who reads it, which stage - before changing
   it. Don't write until you can state what you're about to do in one sentence.
2. **Find the grain.** Locate the closest existing sibling (the system, pass,
   component, or helper most like what you're adding) and mirror its shape. A new
   `System` looks like the other systems; a new component is a plain data struct.
3. **Pick the smallest change that fits.** A method on an existing class beats a
   new file; an enum value beats a parallel type. Ask the three questions in the
   [design guide](guides/design.md#2-three-questions-before-you-touch-a-file).
4. **Respect the seams.** Engine never reaches into the backend; systems talk
   through `FrameContext` and components, not to each other; assets are owned by
   `ResourceManager` and referenced by handle. See
   [engine.md](guides/engine.md#absolutes).
5. **Write it to match its neighbors** ([code-style.md](guides/code-style.md)),
   then run the pre-commit checks in
   [implementation.md](guides/implementation.md#8-pre-commit-quality-pass).
6. **Found a code inconsistency or a stale doc along the way?** Note it and
   surface it - don't silently route around it.

A change is finished when a reader can't tell which lines are new from the style
alone, only from the feature they add.

---

## Map

```
docs/
  README.md            <- you are here: the pre-flight order + working loop
  guides/
    engine.md           what the engine is, values, and has already decided (read 2nd)
    design.md           where a change belongs, and what finished means (read 3rd)
    implementation.md   what makes an implementation good (read 3rd)
    code-style.md       naming / layout / comments / formatting (read 4th)
    review.md           how to tell a drifting design from a working one
    worked-example.md   one complete judgment, end to end
  reference/
    project-overview.md one-page orientation (read 1st)
    architecture.md     engine ownership, stages, FrameContext, directory tree, patterns
    ecs.md              Scene, entities, components, queries, hierarchy
    resources.md        ResourceManager, assets, handles, versioning, by-name identity
    threading.md        the shared-deque ThreadPool + parallelFor
    building.md         CMake targets, modules, flags
    editor.md           panels, gizmos, undo/redo, material preview
    system/
      rendering.md      the fixed 19-pass forward pipeline + RenderView contract
      lighting.md       five light types, LTC area lights, shadows, IBL
      visibility.md     frustum / distance / screen-size culling
      hierarchy.md      world-transform resolve, HierarchyOperations
      animation.md      keyframe tracks, skeletal rigs, poses, GPU skinning
      events.md         typed pub/sub
      io.md             scene / prefab / asset / component serialization, cooked library
      scripting.md      Behavior lifecycle, ScriptComponent, DLL hot-reload
      physics.md        fixed-step rigid bodies, box + capsule colliders, solver, character controller
      ui.md             screen-space in-game UI (canvas/element/image/text/button)
      audio.md          clips, sources, the listener, and the device seam
```
