# Guides

How vkmEngine is worked on: which practices it uses and what good looks like.
Read them in this order the first time; afterwards go to the one whose question
you have.

| Question                                       | Guide                                |
|------------------------------------------------|--------------------------------------|
| What is this engine, and what has been decided? | [engine.md](engine.md)              |
| Where does my change go, and what shape?       | [design.md](design.md)               |
| Is the code good enough - hot paths, threads, failure? | [implementation.md](implementation.md) |
| Does it look like the rest of the engine?      | [code-style.md](code-style.md)       |
| Is what is here the right shape? Is this change ready? | [review.md](review.md)       |
| How do I build it, run it, and check it works?  | [../reference/building.md](../reference/building.md) |

Building a game *with* the engine starts at
[../getting-started.md](../getting-started.md) instead.

---

## How hard is a rule?

Three levels, and every guide of rules opens with an **Absolutes** box listing
the ones in it that admit no judgment:

| Level        | Meaning                                                         |
|--------------|------------------------------------------------------------------|
| **Absolute** | Never, or always. No judgment, no exceptions without the owner.   |
| **Default**  | Do this unless you can say why not - and say it, in the commit.   |
| **Judgment** | How to think about it. The right answer depends on the case.      |

Anything not in an Absolutes box is a default or a judgment; prose that reads as
law outside the box needs fixing - say so.

## When two rules collide

1. **An absolute always wins.** If following one absolute would break another,
   stop and raise it. That is a design problem, not a choice.
2. **[engine.md](engine.md) outranks the others.**
3. **Below that, the value order in [engine.md](engine.md#2-what-it-optimises-for)
   decides** - correctness that survives load, then predictability, then
   simplicity, then measured performance, then features.

## When you disagree with a guide

Say so. Do not quietly do it your way - that leaves two conventions live at
once - and do not follow a rule you believe is wrong. If you must break a rule
to get the work done, **say so in the commit message, with the reason**; an
undocumented exception is drift the next person inherits as precedent.

## Decide, or ask?

Most work needs no permission. The line is **who has to live with being
wrong**, not size.

**Decide it yourself** when the change is reversible and its blast radius is the
thing you were asked to touch: a decomposition, naming, where a helper goes,
whether to extract, what to delete as rot, how to fix the bug in front of you.
Do the work; say what you did.

**Decide, then say so plainly** when you departed from a guide, worked around
something rather than fixing it, or left part of the task undone. The work still
lands; the departure does not go unmentioned.

**Stop and ask** when the answer would bind the future rather than the change:

- Reopening a position in [engine.md](engine.md) - an absolute, a refusal, a
  seam, the value order, a row marked *Position*. A measured row is different:
  when its trigger fires, measure it again, and bring the owner the capture.
- A **format** change: scene, prefab, project, cooked asset. There is no
  migration path.
- **Redesigning something you were asked to fix.** Name it, cost it, let the
  owner choose; never quietly widen a task into one.
- Deleting something you cannot prove is rot rather than design.
- Anything outward-facing: a push, a tag, a release.

When unsure, do the part that is clearly yours and ask about the rest.

## When a guide disagrees with the code

The code says what the engine *is*; the guide says what it *should be*:

- A guide's **example** that does not match the tree is stale. Flag it.
- A **rule** violated in a handful of places marks defects.
- A rule violated nearly everywhere is fiction. Say that too.
