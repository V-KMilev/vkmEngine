# Review Guide

How to look at code that already exists and tell whether the design is drifting.

The other guides assume you are adding something. This one asks a different
question of what is already there: **is this the shape the engine should have?**
It needs its own guide because it cannot be answered from inside a task. A
review framed as "find the defects" finds defects, and every one can be real
while the design goes wrong underneath; nobody hunting bugs in a file concludes
that the file should not exist.

The examples are from this engine. If one disagrees with the source, the source
is right, and the drift is worth saying out loud.

---

## Absolutes

- **Report what you find, even when nobody asked.** Drift nobody hears about
  continues (section 6).
- **Never silently redesign something you were asked to fix.** Name it, cost it,
  let the owner decide.
- **Report the disconfirmations too.** "I expected X and the source says
  otherwise" is what makes the confirmed findings trustworthy.

---

## 1. The tells

Every one of these is the same failure in different clothes: **the code stopped
expressing the design and started depending on someone remembering it.**

| Tell                                | What it means                            |
|-------------------------------------|------------------------------------------|
| An invariant living in a comment    | The structure refuses to encode a rule    |
| A field per case                    | Fixes accreting where a mechanism belongs |
| A guard instead of a state          | A state that was never modelled           |
| One fact stated twice               | Two owners, and one of them will drift    |
| One concept, several mechanisms     | Nobody chose, so everybody chose          |
| A class holding unrelated state     | A container that grew instead of a design |
| The same file fixed again and again | The design is telling you something       |
| A comment that argues               | A decision defended instead of expressed  |

### 1.1 An invariant living in a comment

Prose of this shape:

    "Must precede scene I/O."
    "in the one order that works"
    "Declared before the Engine so it outlives it"

says there is a rule, the code does not hold it, and the next person is expected
to be careful. Swap two lines and the compiler is silent. The fix is almost
never a better comment: it is a type, a constructor that cannot be called out of
order, or one shared sequence with a single home - which is what `bootHost`
(`tools/project_boot.h`) is for the four hosts' prologue.

Some constraints are too small to be worth a type. The editor, the runtime and
the server each declare `ScriptModule` *above* `Engine`, because a behavior is
destroyed during engine teardown and its code must still be mapped; swap the two
lines and the program crashes on exit in a scene with scripts. Two lines in each
of three files, held by a comment. This tell is about noticing; acting is a
judgment.

### 1.2 A field per case

When something new must survive an operation and the answer is another member,
the mechanism is accumulating rather than generalising. `SceneIOController`
recorded a play session as `m_playSnapshot`, then `m_playAssets`, then
`m_playSnapshotDirty`, then `m_playSnapshotHistory` - four fields, four rules,
one incident at a time. Ask: if one more case arrived tomorrow, would this absorb
it or grow again? (They are one `PlaySnapshot` now;
[worked-example.md](worked-example.md) is the whole judgment.)

### 1.3 A guard instead of a state

`refusedDuringPlay()` (`session/scene_io_controller.cpp`) is the whole state
machine for "this must not run during play": a question each save path remembers
to ask. Every new operation must remember it too, and the one that forgets is a
bug an author finds. A state the code can be *in* beats a question every caller
must remember to ask.

### 1.4 One fact stated twice

The defect this engine's reviews have found more than any other. A fact with two
owners - a constant mirrored in GLSL, a rule re-implemented in a second file, a
comment and the code it describes, a doc page and the struct it lists - is
correct only until one of them changes. The second copy is usually right the day
it is written, which is why it survives review.

When you find a fact, ask what *else* states it before asking whether this copy
is wrong. The fix is one owner: a shared constant, a generated list, a helper
both call, or a check that fails the suite when the two disagree
(`tests/docs/docs_tests.cpp` holds the ones for docs against source).

### 1.5 One concept, several mechanisms

Count the ways the engine does one thing. Persistence through a serializer here,
a hand-rolled dump there and a third path in a tool: no one of them is wrong, and
the sum is unmaintainable, because a fix in one never reaches the others and a
reader must learn each variant.

### 1.6 A class holding unrelated state

Read a class's members as a list and ask whether they are one idea.
`SceneIOController` holds the current scene path, a save-as dialog's buffer and
popup flag, a file picker, and the play-session snapshot - which file is open,
what a widget shows, and what Play captured. Each changes for reasons unrelated
to the others.

### 1.7 Repeated fixes in one place

The cheapest and most honest signal, because it is history rather than opinion:

    git log --since=6.months --format=%x01%s --name-only -- src |
        awk '/^\x01fix/{f=1;next} /^\x01/{f=0} f&&/\.(cpp|h)$/{print}' |
        sort | uniq -c | sort -rn

A file that moved counts under each of its names; add them. Read a file's last
five fixes together. If each adds a case rather than removing
the reason for cases, the design is the defect and the fixes are interest. If the
fixes *removed* cases - as the inspector's did, ending in one
`editComponentCard<T>` that snapshots, draws and pushes every card's edit - the
rank is not a verdict.

### 1.8 A comment that argues

A comment defending a decision against an imagined objection is doing a commit
message's job, and usually sits where the author knew the design was
questionable. Treat a paragraph of justification as a pointer to the thing being
justified. How to write the replacement is
[code-style.md](code-style.md#61-what-bounds-a-comment).

---

## 2. What is not a tell

Being wrong in this direction wastes real work:

- **Comment volume.** How many comments a file has says nothing about its design;
  what they *say* is 1.1 and 1.8.
- **File size alone.** A long file of one kind of thing is fine. A short file of
  three kinds is not.
- **A long straight-line function.** Boot sequences and draw loops are long
  because the work is. Length matters when it hides branching.
- **A wide interface.** Twenty methods of one kind of operation is an interface.
  Six across three responsibilities is a god object. Count responsibilities.
- **Code you would have written differently.** Taste is not drift.

---

## 3. How to look

1. **Measure before judging.** Churn, size, counts, and for performance a
   capture. A number kills a bad hypothesis in a minute and makes a good one
   undeniable.
2. **Read the fixes, not just the code.** A file's last five commits say more
   about its design than the file does.
3. **Check a claim the code makes about itself.** A comment saying what a
   function does, a `@param` naming a parameter, a doc listing a struct's fields,
   a count in a guide - grep each against the source. Nearly every defect a
   careful read missed sat next to a comment describing the correct behaviour.
4. **Compile a header on its own**, with the flags from
   `build/compile_commands.json`. A cached build hides one that only compiles by
   luck of what its includer included first.
5. **Strike the comment and reread.** If a competent reader would now ask a
   question, its answer is the only comment needed. If the code becomes
   *unreadable*, the comment was holding up a design that cannot stand.
6. **Use the thing.** Press Play, Pause, Stop; open a project, save it, cook it;
   look at a frame ([design.md](design.md#5-what-finished-means)).

---

## 4. Suffocating or smoothing

Section 1 reads history. This is the forward-looking half: **does this thing make
the next thing easier, or harder?** A **smoothing** design absorbs the next case
without changing shape; a **suffocating** one makes everything work around it.

The tell is in the code, not in your opinion of it:

> **Do people extend it, or route around it?** Count the callers that bypass the
> thing, duplicate a piece of it, or reach past it to what it wraps. A design
> nobody routes around is doing its job however it looks. A design with three
> bypasses is suffocating however clean it looks.

Grep for a second path to the same outcome, a caller reconstructing internals
instead of asking, a helper that exists to avoid the thing, and a comment saying
"we do it this way here because X does not handle Y." For something you are
about to build: **if a hundred things depend on this, is it still the right
shape?** Suffocating designs are rarely bad code; they are reasonable code
holding a responsibility that was the wrong one to centralise.

---

## 5. Patch or redesign

**Redesign when the cost of the next change is rising** - each fix larger than
the last, or adding a case rather than removing the reason for cases.
**Patch when the thing is done and merely imperfect** - it works, it is not
growing, nothing new is being built on it.

- **A redesign is not a rewrite.** Take the smallest change that removes the
  reason the fixes keep coming; leave the rest.
- **Say it even when the answer is "not now."** A named, deferred problem is a
  decision. An unnamed one is a surprise later.

---

## 6. Reporting what you find

On a codebase too large to hold in one head, reporting is a duty:

- **Surface it unprompted.** The owner should not have to suspect a problem to
  hear about it.
- **Lead with the evidence** - `file:line`, the churn count, the four fields
  added one at a time. A structural claim without evidence is taste.
- **Separate confirmed from suspected,** and say which is which.
- **Report the disconfirmations.**
- **Give the cost both ways** - what the redesign takes, and what leaving it
  costs. The decision is the owner's; the estimate is yours.

---

## 7. Reviewing a change

Everything above judges code that exists. A change under review is judged by one
standard: **approve it when it leaves the engine better than it found it**, even
if it is not how you would have written it. Perfect is not the bar; better is,
and a change that is better should not wait on taste.

Look in the order that finds the expensive problems first:

1. **The why.** The commit message says why the change exists. If it cannot, the
   change is not ready, whatever the diff looks like.
2. **The shape.** Is it where [design.md](design.md#22-where-does-it-belong)
   puts it? Does it add a case beside three that arrived the same way (1.2)?
3. **The seams.** Anything naming a format, an order, a lifetime or an identity
   gets the full argument now ([design.md](design.md#3-how-much-thinking-does-this-deserve)),
   because it is the part that cannot be taken back.
4. **Correctness.** The failure test, the threads, determinism
   ([implementation.md](implementation.md)). A fix comes with a test that was
   seen to fail without it.
5. **What it claims.** Every performance claim carries a capture; every comment
   and doc line states something the code does. `tools/comment_sweep` lists the
   lines elsewhere that name what the change touched; each is read against the
   new code, because that is where a true comment becomes a false one.
6. **The mechanics** last - [code-style.md](code-style.md) is checkable, and
   mostly checked.

Before a release, the comments the release touched get one more read, by a
reviewer whose only job is to refute them: open the code each sentence
describes and check it is still true. A rewrite of a comment is where invented
mechanisms come from, and a second reader with no stake in the first is what
catches them.

Say which of your comments block and which are suggestions. A defect, a broken
absolute, an unmeasured performance claim, docs that did not ship and a
half-finished refactor block. Everything in section 2 does not.

