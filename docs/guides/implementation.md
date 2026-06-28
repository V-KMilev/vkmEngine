# Implementation Guide

What separates a good implementation from one that merely compiles. The
[development.md](development.md) guide decides *where* a change goes and *what
shape* it takes; this one is the quality bar for the code that fills that shape.
Mechanics (naming, layout) are in [code-style.md](code-style.md).

The one-sentence version: **write the simplest thing that solves today's
problem cleanly, and would still fit if the engine doubled in size.**

---

## 1. Solve today's problem, not tomorrow's

Do not design for hypothetical futures. Speculative complexity is paid for now
and rarely matches the future that actually arrives.

- **No flags "in case we ever need it."** Add the flag the day a real caller
  needs it, not the day you imagine one might.
- **No abstractions for a single user.** If only one class implements an
  interface, the interface is a virtual call wearing a costume. Wait for the
  second user, then extract.
- **No framework code without a feature.** Internal machinery justifies itself
  by removing duplication that already exists, not duplication that might appear.

When in doubt, **write the concrete version first.** Extracting an abstraction
from two working implementations is straightforward; predicting the right
abstraction from zero is guesswork.

---

## 2. Simple beats clever

Pick the construct that costs the reader the least:

- A clear `if` / `else` beats a virtual hierarchy that exists to express two
  cases.
- Three slightly repetitive lines beat a premature template.
- A free function beats a singleton when one would do.
- A plain `struct` beats a `class` when there is no invariant to protect.

"Clever" is a warning sign. If a line takes ten seconds to write and the next
reader thirty to understand, you owe the codebase the rewrite. Most performance
wins in this engine come from data layout - `SparseSet`, generational handles,
batched draws - not from clever syntax. There is almost never a reason to be
cute.

---

## 3. Generic enough to not bite later - but no more

This is the balance the project cares about most, and it cuts both ways.

The good abstractions in this engine - `RenderBackend`, `System`,
`SparseSet<T>`, `Handle<T>`, the `core/reflect.h` field reflection - are excellent
*because each removes real duplication and serves many callers.* That is what
"generic so it does not bite us long term" means: an abstraction that absorbs
the next ten similar cases without change.

The failure mode is the opposite: a **speculative** abstraction introduced
before its weight is justified. A `Manager` / `Factory` / `Helper` with one user
is not generality, it is overhead. The test is not "could this be reused?"
(anything could) but **"is it reused, or about to be, by a concrete second
case I can name?"**

So the rule of thumb:

- Building the *second* very-similar thing? That is the moment to extract the
  shared shape - you now have two real cases to generalize over.
- Building the *first*? Write it concretely. The right abstraction will be
  obvious later and wrong if guessed now.

Generality is earned by duplication you can point at, not promised against
duplication you imagine.

---

## 4. Clean, readable code is the deliverable

Code is read far more often than written. The compiler does not care how
readable a function is; the next person opening the file does.

- **Name things for meaning, not type.** `entity` beats `id`; `worldMatrix`
  beats `m`. The name carries the comment you did not write.
- **Keep functions short enough to see end-to-end.** If a function spans more
  than a screen, a sub-step probably wants to become its own named helper.
- **One responsibility per function, one shape per file.** A file mixing
  unrelated types or behaviors is a design that slipped.
- **Early-return / early-continue over deep nesting.** The reader should not
  have to track which branch they are in three levels deep.
- **Delete dead code.** Unused code is not free - every reader must decide
  whether it matters. Commit the deletion; git remembers.

If you cannot say what a function does in one sentence, it probably does too
much.

---

## 5. Comment only the non-obvious *why*

The default is no comment - good names and structure do the explaining. Write a
comment when, and only when, a reader would otherwise have to ask: why does this
exist, what invariant does it hold, why is the obvious alternative wrong. Never
comment to restate the code or to reference a task or commit. The mechanics of
*which* comment style live in [code-style.md](code-style.md#6-documentation-and-comments).

---

## 6. The long-term test

Before committing a non-trivial change, ask:

1. **Will the next person understand this in five minutes** without asking you?
2. **If the engine doubles in size,** does this still fit, or will it have to be
   rewritten?
3. **If you deleted the comments,** is the code still understandable?

If the answer to any of these is "no," the change is not done yet.

---

## 7. Design-level anti-patterns

These are the failures of judgment, distinct from the mechanical slips listed in
[code-style.md](code-style.md#12-anti-patterns-reviewers-flag):

1. **Speculative abstraction.** A `Manager` / `Factory` / `Helper` with one user.
   Inline it until a second user appears (section 3).
2. **Premature generality.** Templating, virtualizing, or parameterizing a thing
   that has exactly one concrete form.
3. **Half-finished refactors.** If you rename, rename everywhere; if you move a
   file, move its callers. Half-done is worse than not-done - it leaves two
   conventions live at once.
4. **Behavior on data components.** Logic creeping into an ECS component instead
   of the system that owns the behavior.
5. **Crossing a seam to save a few lines.** Reaching into the backend, calling
   one system from another, or bypassing `ResourceManager` to dodge a small
   amount of plumbing. The plumbing is the design ([development.md](development.md#4-the-seams-you-must-not-cross)).
6. **Dead code "kept just in case."** Delete it; git is the just-in-case.

---

## 8. Pre-commit quality pass

Beyond the mechanical checklist in [code-style.md](code-style.md#11-quick-checklist-before-pushing),
ask the three judgment questions:

- [ ] **Design:** does this fit the engine, or sit awkwardly beside it? Could it
      be simpler?
- [ ] **Speculation:** is anything here - a flag, a virtual, an abstraction -
      without a real, nameable user today?
- [ ] **Readability:** can the next person read this top-to-bottom and follow it
      without asking you?

If all three pass and the code matches its neighbors, it is ready.
