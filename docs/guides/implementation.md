# Implementation Guide

The bar for the code inside a change: **the simplest thing that solves today's
problem, in a shape that would still fit if the engine doubled.**
[design.md](design.md) decides where it goes; [code-style.md](code-style.md)
holds the mechanics.

---

## Absolutes

- **No abstraction without a second concrete user you can name today** -
  section 1.
- **Delete rot.** Never comment it out, never keep it "just in case"; git is the
  just-in-case. What counts as rot is section 6.
- **Never leave a refactor half-done.** Rename everywhere or nowhere; move a file
  and its callers together - section 7.
- **No `thread_local` in an engine header** - section 3. The build enforces it
  (`testNoEngineHeaderCarriesAThreadLocal`).

---

## 1. Concrete first

Write the first one concretely; extract on the second, when you can point at the
duplication. An abstraction guessed from one case is wrong in the way you did not
imagine. A virtual with one implementation, a flag with no caller, a `Manager`
or `Factory` with one user - each is overhead with a name.

Pick the construct that costs the reader least: an `if` over a hierarchy for two
cases, three similar lines over a template, a free function over a singleton, a
`struct` over a `class` when there is no invariant. Never cross a seam to save a
few lines.

Simple is not small: when the explicit version stays correct as things lean on
it, it beats the compact one.

---

## 2. Know how often it runs

Before the code: how many of these are there, how often does this run, and who
reads what it writes. **Hot** runs per frame times per entity, per draw, per
tick times per body or contact, per peer per snapshot. **Cold** runs at boot, on
load, in the cooker, or once per editor gesture.

Hot code:

- **allocates nothing in steady state.** A buffer lives on its owner, is
  `clear()`ed each frame and keeps its high-water capacity. `reserve()` before
  a loop that appends a known count.
- **walks dense arrays.** Iterate a `SparseSet` densely; do not look up by id
  inside a loop that could have walked the storage. Copy trivially-copyable bulk
  with `memcpy`.
- **does not lock, throw or call through `std::function`** per element.
- **touches each byte as few times as it can** - pass an index, not a copy.
- **on the GPU side, counts binds and draws.** The driver validates each
  framebuffer bind and each draw after a state change; that, not the shader, is
  what a pass costs.

Cold code is written for the reader: no hot-path idiom in a loader or the
cooker.

A claim that something is faster is a capture before and after, on the scenes it
touches ([../reference/building.md](../reference/building.md#measuring-a-change)).
Inside the run-to-run noise, the simpler code stays.

**Tune for the machines a game runs on, not the one you measure on.** A
measured constant - a chunk count, a sample budget - sits in the middle of the
swept range that performs alike, not on a peak. What the hardware can report
(thread count, texture limits, alignment) is asked, not assumed. A win on one
driver alone is kept only if it costs nothing on the others.

---

## 3. Threads

One thread runs the frame. A system that needs more cores forks inside itself
with `parallelFor` and joins before it returns
([../reference/threading.md](../reference/threading.md)). Within that:

- **A parallel body writes only its own index's output**, or per-thread scratch
  sized before the fork. Never a shared container, never a counter without an
  atomic.
- **A parallel body never changes the component graph** - no create, destroy,
  add or remove.
- **GL is the main thread's.** Workers decode; the frame uploads.
- **Background work returns through a queue.** An asset decode runs on the pool
  via `addTask` and hands its result to `AsyncLoadQueue`; `AsyncLoaderSystem`
  applies it on the main thread, and a result for an asset that was replaced in
  the meantime is recognised and dropped.
- **`thread_local` scratch lives in a `.cpp`, in an anonymous namespace** - never
  in an engine header. Instantiated in a gameplay module with a destructor, it
  pins the module in memory for the frame thread's life, so every script reload
  leaks the old module.

---

## 4. Determinism

The simulation is deterministic: a client re-runs ticks the server disagreed
with and must arrive where the server did.

- **Simulation advances in `fixedUpdate`, by `ctx.clock.getFixedStep()`**, never
  by the frame delta and never by the wall clock.
- **Order is by slot.** The solver canonicalises contacts on slot, and
  `BehaviorSystem` runs behaviours in slot order. Anything that iterates entities
  into simulation state does the same; never let a hash map's iteration order,
  a pointer's value or a thread's finishing order decide it.
- **Randomness that feeds re-simulated state is owned and seeded.**
  `Math::Random::rng()` is per-thread and clock-seeded: right for a sound's pitch
  or a choice only the server makes, wrong for anything a client replays. A
  system that needs repeatable spread owns a `Math::Rng` with a fixed seed, as
  `ParticleSystem` does.

A parallel loop is deterministic when each index writes only its own output
(section 3) and any merge runs in index order after the join.

---

## 5. When it fails

Ask of every function: what does this do when the asset is missing, the file will
not parse, the handle is stale, the device will not open? "It cannot happen" is
an answer only if something enforces it. **Silent wrong behaviour is worse than a
crash.**

| Context                          | Mechanism                          |
|----------------------------------|------------------------------------|
| Preconditions (programmer error) | `VKM_ASSERT(condition, "message")` |
| Initialization failures          | `throw std::runtime_error(...)`    |
| Runtime lookup not found         | return `nullptr` or `false`        |
| Invalid handle                   | null sentinel (index 0)            |

- `VKM_ASSERT` compiles to nothing in a shipping build (or with `VKM_ASSERTS`
  off), so its condition has no side effects. It lives in the foundation types - `Scene`, `SparseSet`,
  `SlotAllocator`, `ResourceManager`, the hierarchy operations - where a broken
  precondition corrupts shared state. A destructive operation **asserts and
  guards** the same condition: the assert is for the programmer who broke it,
  the guard for the player who ships it. The backend, the editor and the hosts
  guard and degrade.
- **Exceptions are for startup, loading and explicit recovery boundaries**, never
  a hot path.
- **An enum that indexes a hand-written table gets a `Count` sentinel and a
  `static_assert` on the table's length** (`STAGE_NAMES` in `core/engine.cpp`).
  Better still, generate the table from the same list as the enum - `TYPE_DIRS`
  expands from `VKM_ASSET_KINDS` - and there is nothing left to compare.
- **Anything read from outside the process is bounded before it is trusted** -
  a count divided before it is multiplied, a length checked before it is
  allocated. Each reader refuses what it cannot represent rather than clamping it
  into something wrong.

Three channels carry a failure; pick by **who caused it and who can act on it**:

- **`LOG_ERROR` and friends** reach the developer's console. Every failure logs.
- **`reportError(category, source, message)`** (`debug/engine_error_log.h`)
  reaches the editor's Errors tab and the cooker's summary: a recoverable failure
  the **project author** caused - a throwing script hook, an asset that will not
  resolve, a module that will not load. It logs too, so it never wants a
  `LOG_ERROR` beside it.
- **`state.pushToast(...)`** reaches the author beside the gesture, for an
  **editor action** that succeeded or failed. An editor operation that fails
  takes the log and the toast, not `reportError` - the author caused nothing.
  Do not add a second log line restating what the layer below already printed.

---

## 6. Delete rot, keep design

**Rot goes:** an orphan nothing reaches, a path superseded by its replacement,
state nobody reads, a doc describing something that no longer exists.

**Design stays:** a natural accessor, or an API that completes a type's obvious
surface, **even with no caller yet.** The question is never "is this called?" but
**"is this left over, or part of the shape?"**

---

## 7. Changing a shape its users rely on

The engine is designed for itself
([design.md](design.md#12-the-engine-is-designed-for-itself)), so a change of
shape carries its users with it, in one pass:

1. **Change the engine** to the shape it should have. Delete the old one; keep
   no alias, overload, fallback or second spelling for anyone.
2. **Find every user**: `src/`, `app/`, `tests/`, `templates/`, `examples/`,
   `content/` and the docs - their code and their saved scenes, prefabs and
   recipes.
3. **Port each one.** Code moves to the new shape. A saved file is re-saved in
   the new format, by the editor or a one-off script that is not committed; the
   engine gains no reader for the old one
   ([engine.md](engine.md#51-what-each-formats-version-gate-actually-does)).
4. **Build and run each project** you touched.

---

## 8. Names and shape

A name carries the comment you did not write: `entity`, not `id`;
`worldMatrix`, not `m`. A function is one sentence without an "and"; one
responsibility per function, one kind of thing per file. Return early rather than
nest.

Past a screen, look for a sub-step that wants a name - unless it is a boot
sequence or a draw loop, which are long because the work is
([review.md](review.md#2-what-is-not-a-tell)).

---

## 9. A long comment is a diagnosis

What a comment may say, and how long it may run, is
[code-style.md](code-style.md#6-documentation-and-comments). A long declaration
block a caller needs is fine; the tenth line of `//` in the middle of a function
means one of three things:

1. **The code is wrong.** The comment apologises for logic that is hard to
   follow; fix the decomposition or the naming. Try this first.
2. **The material is real and belongs elsewhere.** What a caller needs goes in
   the declaration's block; what is about the subsystem goes in `docs/reference/`
   with a one-line pointer left behind. It moves; it is never deleted.
3. **You are arguing, not explaining.** State the decision and the constraint it
   protects in two lines, then stop.

---

## 10. Checks with an answer

Run these on your own change.

- **The derived-state test.** For every member: could it be computed from the
  others? Stored state that could be derived is kept in sync by hand. Store it
  anyway only when the computation is measurably hot.
- **The second-user test.** For every abstraction, name the second caller, today
  (section 1).
- **The leftover-or-design test.** For every deletion candidate (section 6).
- **The strike test.** For every comment: delete it and reread
  ([code-style.md](code-style.md#61-what-bounds-a-comment)).
- **The failure test.** For every input from outside this function (section 5).
- **The route-around test.** Would the next feature extend this, or work around
  it? ([review.md](review.md#4-suffocating-or-smoothing))
- **The mutation test.** For every test that guards a fix: undo the fix, rebuild,
  and watch the test fail. A test you have not seen fail may be testing nothing.
- **The measurement test.** For every performance claim, the capture (section 2).
- **The load test.** If a hundred things end up depending on this, is it still the
  right shape - or the thing everything works around?
