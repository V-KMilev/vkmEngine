# Implementation Guide

What separates a good implementation from one that merely compiles.
[design.md](design.md) decides *where* a change goes and *what shape* it takes;
this is the bar for the code that fills that shape. Mechanics are in
[code-style.md](code-style.md).

The one-sentence version: **write the simplest thing that solves today's problem
cleanly and would still fit if the engine doubled in size.**

---

## Absolutes

- **No abstraction without a second concrete user you can name today.** Stated as
  a refusal in [engine.md](engine.md#3-what-the-engine-refuses); how to apply it
  is section 3 below.
- **Delete rot.** Never comment it out, never keep it "just in case". Git is the
  just-in-case. What counts as rot is section 4.
- **Never leave a refactor half-done.** If you rename, rename everywhere; if you
  move a file, move its callers. Half-done leaves two conventions live at once,
  which is worse than not starting.

---

## 1. Simplicity is the means, not the end

The end is code that is still right in three years, because this engine is built
to be finished rather than rewritten. Simple designs earn their place by staying
correct as things pile on them; clever ones fail quietly the moment the
assumption they were built around stops holding.

Which means **smaller is not automatically better.** If the explicit version is
the one that stays correct under load, it wins over the compact one that reads
well today and breaks the first time something unexpected leans on it. Fewer
lines is evidence of a simpler design, not a substitute for one.

---

## 2. Solve today's problem, not tomorrow's

Speculative complexity is paid for now and rarely matches the future that
actually arrives.

- **No flags "in case we ever need it."** Add the flag the day a real caller
  needs it.
- **No abstractions for a single user.** If only one class implements an
  interface, the interface is a virtual call wearing a costume.
- **No framework code without a feature.** Internal machinery justifies itself by
  removing duplication that already exists, not duplication that might appear.

When in doubt, write the concrete version first - section 3 says when to stop.

### 2.1 Simple beats clever

Pick the construct that costs the reader least:

- A clear `if` / `else` beats a virtual hierarchy expressing two cases.
- Three slightly repetitive lines beat a premature template.
- A free function beats a singleton when one would do.
- A plain `struct` beats a `class` when there is no invariant to protect.

"Clever" is a warning sign. If a line takes ten seconds to write and the next
reader thirty to understand, you owe the codebase the rewrite. Most performance
wins here come from data layout - `SparseSet`, generational handles, batched
draws - not from clever syntax.

---

## 3. Generic enough not to bite later, and no more

This cuts both ways, and the project cares about it more than almost anything.

The good abstractions in this engine - `RenderBackend`, `System`,
`SparseSet<T>`, `Handle<T>`, the field reflection in `core/reflect.h` - are good
*because each removes real duplication and serves many callers.* That is what
"generic so it does not bite us long term" means: an abstraction that absorbs the
next ten similar cases without change.

The failure mode is the opposite: a speculative abstraction introduced before its
weight is justified. A `Manager` / `Factory` / `Helper` with one user is not
generality, it is overhead. The test is not "could this be reused?" - anything
could - but **"is it reused, or about to be, by a concrete second case I can
name?"**

- Building the *second* very-similar thing? That is the moment to extract.
- Building the *first*? Write it concretely. The right abstraction will be
  obvious later and wrong if guessed now.

Generality is earned by duplication you can point at, not promised against
duplication you imagine.

---

## 4. Delete rot, not merely the unused

These are not the same thing and the difference has cost real work.

**Rot goes.** An orphan nothing reaches. A path superseded by its replacement.
State nobody reads. A doc describing something that no longer exists. Delete it
and commit the deletion.

**Design stays.** A natural accessor, or an API completing a type's obvious
surface, stays **even with no caller yet.** It is not debt, it is the shape of
the thing, and deleting it only churns the type the day someone needs it.

The question is not "is this called?" but **"is this left over, or is it part of
the design?"** Leftovers go. Design stays.

---

## 5. Readable code is the deliverable

Code is read far more often than written. The compiler does not care how
readable a function is; the next person opening the file does.

- **Name for meaning, not type.** `entity` beats `id`; `worldMatrix` beats `m`.
  The name carries the comment you did not write.
- **Keep functions short enough to see end-to-end.** Past a screen, a sub-step
  probably wants to be its own named helper.
- **One responsibility per function, one shape per file.**
- **Early-return over deep nesting.** The reader should not have to track which
  branch they are in three levels down.

If you cannot say what a function does in one sentence, it probably does too
much.

---

## 6. Comment only the non-obvious why

The default is no comment - names and structure do the explaining. Write one only
when a reader would otherwise have to ask: why does this exist, what invariant
does it hold, why is the obvious alternative wrong. What *bounds* a comment -
lines inside a function, relevance to a caller on a declaration - has one home,
[code-style.md](code-style.md#61-what-bounds-a-comment), and this guide does not
restate it. What follows is what to do when yours is too long.

**A long comment inside a function is a diagnosis before it is a comment.** A
declaration block is not - twenty lines a caller could not have inferred is the
right answer there. But when you find yourself writing the tenth line of `//` in
the middle of the work, one of three things is true:

1. **The code is wrong.** The logic is genuinely hard to follow and the comment
   is apologising for it. Fix the decomposition or the naming. This is the only
   one of the three that improves the engine, so try it first.
2. **The material is real, and belongs where a reader will find it.** A threading
   contract, an upstream bug, why a format is shaped as it is. If a *caller*
   needs it, it belongs in the declaration's block, at whatever length that
   takes. If it is about the subsystem rather than about any one call, it belongs
   in `docs/reference/` with a one-line pointer left behind. Either way it moves;
   it is never deleted.
3. **You are arguing, not explaining.** The reader needs the decision and the
   constraint it protects, not the case for it. Two lines, then stop.

Verbosity is not thoroughness. A comment costs every future reader the time to
read it and the doubt about whether it is still true, and an out-of-date essay is
worse than none because it is confidently wrong.

---

## 6.1 Tests you can actually run

The rest of this guide is principle. These are checks with an answer, and they
catch most of what "not well done" turns out to mean here.

**The one-sentence test.** Say what the function does in one sentence. If the
sentence needs an "and", the function probably wants splitting.

**The derived-state test.** For every member, ask whether it could be computed
from the others instead of stored. Stored state that could be derived has to be
kept in sync by hand, and the day someone forgets is a bug that reproduces only
in sequence. Store it anyway if the computation is measurably hot - but that is a
performance decision you should be able to defend, not the default.

**The second-user test.** For every abstraction: name the second concrete caller,
today. Cannot? Inline it (section 3).

**The leftover-or-design test.** For every deletion candidate: is this left over,
or is it part of the shape of the type? Leftovers go, design stays (section 4).

**The strike test.** For every comment: delete it and reread. If a competent
reader who knows this engine would now ask a question, keep the answer to that
question and nothing else. If they would ask nothing, it was never load-bearing.

**The failure test.** What does this do when the asset is missing, the file will
not parse, the handle is stale, the device will not open? "It cannot happen" is
an answer only if something enforces it. Silent wrong behaviour is the worst
outcome available, worse than a hard failure, because it costs somebody a day.

**The route-around test.** Would the next feature extend this, or work around it?
See [review.md](review.md#4-suffocating-or-smoothing).

---

## 7. Design anti-patterns

Failures of judgment, distinct from the mechanical slips in
[code-style.md](code-style.md#12-anti-patterns-reviewers-flag):

1. **Speculative abstraction.** A `Manager` / `Factory` / `Helper` with one user;
   a template or a virtual over exactly one concrete form. Same defect either
   way - generality bought before there was anything to spend it on (section 3).
2. **Half-finished refactors.** Two conventions live at once.
3. **Behavior on data components.** Logic creeping into a component instead of
   the system that owns it.
4. **Crossing a seam to save a few lines.** The plumbing is the design.
5. **Dead code kept just in case.** Rot, not merely the uncalled - section 4.
6. **The comment that argues.** A block defending a decision, or carrying section
   headings, instead of stating the constraint it protects. Length is not the
   defect ([code-style.md](code-style.md#61-what-bounds-a-comment)); section 6
   says which of the three fixes applies.

---

## 8. Before you commit

Beyond the mechanical checklist in
[code-style.md](code-style.md#11-quick-checklist-before-pushing):

- [ ] **Design.** Does this fit the engine, or sit awkwardly beside it? Could it
      be simpler?
- [ ] **Speculation.** Is anything here - a flag, a virtual, an abstraction -
      without a real, nameable user today? An accessor that completes a type's
      obvious surface is not speculation and stays (section 4).
- [ ] **Readability.** Can the next person read this top to bottom and follow it
      without asking you?
- [ ] **Comments.** Is every one load-bearing? Strike each and reread; if nothing
      is lost, it should not have been written.
- [ ] **Load.** If a hundred things end up depending on this, is it still the
      right shape - or does it become the thing everything works around?

The last one matters most for anything other code will build on, and least for a
leaf. But see [design.md](design.md#3-how-much-thinking-does-this-deserve) on how
often a leaf turns out not to be one.
