# Guides

Everything needed to work on vkmEngine the way it is meant to be worked on. Read
in this order the first time; afterwards go to the one whose question you have.

| Question                                       | Guide                                |
|------------------------------------------------|--------------------------------------|
| What is this engine, and what has been decided? | [engine.md](engine.md)              |
| Where does my change go, and what shape?       | [design.md](design.md)               |
| Is the code good enough - hot paths, threads, failure? | [implementation.md](implementation.md) |
| Does it look like the rest of the engine?      | [code-style.md](code-style.md)       |
| Is what is here the right shape? Is this change ready? | [review.md](review.md)       |
| What does a full judgment actually look like?  | [worked-example.md](worked-example.md) |
| How do I build it, run it, and check it works?  | [../reference/building.md](../reference/building.md) |

These are for working *on* the engine. Someone building a game *with* it wants
[../getting-started.md](../getting-started.md) and the pages
[../README.md](../README.md) routes them to.

---

## How hard is a rule?

Before any rule, know whether you may depart from it. Three levels, and every guide of rules opens
with an **Absolutes** box listing the ones in it that admit no judgment:

| Level        | Meaning                                                         |
|--------------|------------------------------------------------------------------|
| **Absolute** | Never, or always. No judgment, no exceptions without the owner.   |
| **Default**  | Do this unless you can say why not - and say it, in the commit.   |
| **Judgment** | How to think about it. The right answer depends on the case.      |

Anything not in an Absolutes box is a default or a judgment. If a guide states
something as though it were law and it is not in the box, the box is right and
the prose needs fixing - say so.

## When two rules collide

1. **An absolute always wins.** If following one absolute would break another,
   stop and raise it. That is a design problem, not a choice.
2. **[engine.md](engine.md) outranks the others.** It holds what the engine is
   and what has been decided; the rest is how to build well within that.
3. **Below that, the value order in [engine.md](engine.md#2-what-it-optimises-for)
   decides** - correctness that survives load, then predictability, then
   simplicity, then measured performance, then features.

## When you disagree with a guide

Say so. Do not quietly do it your way, and do not follow a rule you believe is
wrong just because it is written down. A guide that is wrong stays wrong until
someone says so, and silent deviation leaves two conventions live at once, which
is worse than either.

If you must break a rule to get the work done, break it and **say so in the
commit message, with the reason.** A documented exception is a decision. An
undocumented one is drift that the next person inherits as precedent.

## Decide, or ask?

Most work needs no permission. The line is not about size, it is about **who has
to live with being wrong.**

**Decide it yourself** when the change is reversible and its blast radius is the
thing you were asked to touch. Choosing a decomposition, naming, where a helper
goes, whether to extract, what to delete as rot, how to fix the bug in front of
you. Ask nothing; do the work; say what you did.

**Decide, then say so plainly** when you departed from a guide, worked around
something rather than fixing it, or left part of the task undone. The work still
lands. What is not acceptable is the departure going unmentioned.

**Stop and ask** when the answer would bind the future rather than the change:

- Reopening anything in [engine.md](engine.md) - a settled decision, a refusal, a
  seam, the value order.
- A **format** change: scene, prefab, project, cooked asset. There is no
  migration path, so a wrong call breaks every existing file permanently.
- **Redesigning something you were asked to fix.** Name it, cost it, let the
  owner choose. Never do it silently, and never quietly widen a task into one.
- Deleting something you cannot prove is rot rather than design.
- Anything outward-facing: a push, a tag, a release.

When you are unsure which side a thing falls on, do the part that is clearly
yours, and ask about the part that is not - rather than blocking the whole task
on one question.

## When a guide disagrees with the code

The code is the source of truth for what the engine *is*; the guide is the
source of truth for what it *should be*. So:

- If a guide's **example** does not match the tree, the example is stale. Flag it.
- If a guide's **rule** is violated in a handful of places, those are defects.
- If a rule is violated nearly everywhere, the rule is fiction. Say that too - a
  rule nobody follows teaches every reader that the guides can be ignored.
