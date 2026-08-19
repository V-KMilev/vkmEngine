# Development Guide

How to look at a problem before you write code, and how to fit the answer to
the engine instead of bolting it on. The [code-style.md](code-style.md) guide
answers "what does compliant code look like?" This one answers "where does my
change belong, and what shape should it take?" The quality bar for the code
itself is in [implementation.md](implementation.md).

---

## 1. The goal you are building toward

vkmEngine is a long-term engine, measured in years rather than releases. The
point is not to build something easy to change later - it is to build something
that does not need changing. Rewriting a subsystem every six months is the
failure this project is trying to avoid.

That target changes what "good" means, because **the cost of a design is paid by
everything built on top of it.** A wrong call in a leaf is an afternoon's work
whenever you get to it. A wrong call in a foundation is not one problem, it is
the hundred things that assumed it, and by the time the cost is obvious the
foundation is load-bearing and cannot be moved. Three years in, you are not
fixing the mistake, you are fighting everything standing on it.

So the prime directive is: **make the engine more predictable, not less.** A
change that follows the existing grain leaves the codebase easier to reason
about. A change that invents its own conventions, seams, or layering makes the
next person's job harder even if it works.

And scrutiny scales with load. The ECS, `FrameContext`, `Handle<T>` and the
`ResourceManager` it indexes, the scene and asset formats, the `RenderBackend`
seam - these carry everything else, and a design flaw in one of them compounds
for the life of the project. Spend the thinking there. A helper with three
callers does not deserve the same argument, and treating every change as equally
weighty is its own way of getting the important ones wrong.

---

## 2. The engine has a grain - work with it

Before writing anything new, look at how the existing version of that thing is
shaped, and match it:

- A new **system** plugs into the per-stage pipeline exactly like every other
  `System`: subclass `System`, implement `update(FrameContext&)`, register at a
  `SystemStage`. It reads and writes through `FrameContext`, not through globals.
- A new **ECS component** is a plain data struct - bare members, no behavior.
  Logic that operates on it lives in a system, not on the component. (`Transform`
  is the model: data plus *static* helpers, no instance methods that mutate.)
- A new **render pass** looks like the other passes and stays *inside* the
  backend. There is no engine-level pass abstraction; passes are an OpenGL
  implementation detail.
- A new **asset kind** is a `Resource` subclass stored in `ResourceManager`,
  reached through a typed `Handle<T>`, and registered with the asset factories
  so serialization can cold-load it.

If your new thing cannot be expressed in the same shape as its siblings, that is
a signal - either you have misunderstood the existing pattern, or you are
solving a genuinely new kind of problem (rare; double-check before assuming it).

---

## 3. Three questions before you touch a file

Ask these in order. Most changes are settled by question 1 or 2.

### 3.1 Does this already exist?

Half-written abstractions are worse than none. If 80% of what you need is
already in `HierarchyOperations`, the `tools/generator/` helpers, or a culling
stage, extend that instead of starting a parallel utility. Search first; the
engine has more reusable machinery than is obvious at a glance (`SparseSet<T>`,
`SlotAllocator`, `ThreadPool::parallelFor`, the `core/reflect.h` field reflection,
the event pub/sub).

### 3.2 Where does it belong?

The directory tree encodes responsibility. Let it place your code:

| If the code is...                          | It belongs in...                    |
|--------------------------------------------|-------------------------------------|
| Per-frame behavior over the scene          | `src/engine/system/<name>/` (a `System`) |
| Pure data attached to an entity            | `src/engine/ecs/component/`         |
| Low-level container / handle / type machinery | `src/engine/core/memory/`        |
| GPU-specific work                          | `src/backend/opengl/` (behind `RenderBackend`) |
| Editor-only UI / interaction               | `src/editor/`                       |
| Asset creation, loading, import            | `src/tools/generator/` or `src/tools/loader/` |
| Scene / asset / component (de)serialization | `src/engine/io/`                   |

If your code does not fit anywhere obvious, the tree is telling you the design
is off - stop and reconsider before forcing it in.

### 3.3 What is the smallest change that fits?

A one-line method on an existing class almost always beats a new helper file. A
new enum value beats a new parallel type. Prefer the change that adds the least
new surface area while still being clean. (The discipline of *small and simple*
is developed in [implementation.md](implementation.md).)

---

## 4. The seams you must not cross

The engine's architecture is held together by a few deliberate boundaries.
Respecting them is non-negotiable, because every one of them exists to keep a
subsystem replaceable or a hot path fast.

- **Engine never reaches into the backend.** All engine-to-GPU traffic goes
  through the `RenderBackend` interface (`init` / `render`). The
  `RenderSystem` builds a backend-agnostic `RenderView` each frame and hands it
  over. This is what keeps a future Optix/CPU backend possible. Do not include a
  `gl_*` header from engine code.
- **`MaterialAsset` is the renderer contract.** The forward shader implements
  the PBR spec that `MaterialAsset` describes. Change the material model and the
  shader together; do not encode rendering decisions outside that contract.
- **`Scene` is an open, type-erased registry.** Any type can be a component
  without modifying `Scene`. Never edit `Scene` to "add support" for a component
  - just `scene.add<T>(entity, ...)`.
- **`ResourceManager` owns assets; the scene references handles.** Components
  hold `Handle<T>`, not asset pointers or copies. Serialization stores asset
  *names*, resolved back to handles on load via `findByName`.
- **`FrameContext` is the only per-frame channel between systems.** Systems
  communicate by writing components or `FrameContext` fields that a later stage
  reads (e.g. `VisibilitySystem` populates `ctx.visibility`, `RenderSystem`
  consumes it). They do not call each other directly.
- **Stage order is the schedule.** A system runs at exactly one `SystemStage`,
  and stages run in declaration order: Input, Simulation, Transform, Visibility,
  Render, UI. Place a new system by *responsibility* (physics -> Simulation,
  derived transforms -> Transform), and rely on ordering rather than manual
  sequencing.

When a task seems to need crossing one of these seams, that is usually the most
important thing to surface and discuss - not to quietly work around.

---

## 5. How to approach a new feature

1. **Read the system it touches first.** Find the relevant doc in
   [../reference/](../reference/) and the actual source. Understand the data
   flow (who writes the component, who reads it, which stage) before changing it.
2. **Find the grain.** Locate the closest existing sibling - the pass, system,
   component, or helper most like what you are about to add - and mirror its
   shape.
3. **Pick the smallest fitting change** (section 3.3) and the simplest construct
   that expresses it ([implementation.md](implementation.md)).
4. **Trace the consequences across the seams** (section 4): does this need a new
   `FrameContext` field? A new component? A backend change behind
   `RenderBackend`? A serializer update in `io/`?
5. **Write it to match its neighbors** so the diff reads like it was always
   there ([code-style.md](code-style.md)).

A change is finished when a reader cannot tell which lines are new from the
style alone - only from the feature they add.
