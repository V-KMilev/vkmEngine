# Review Guide

How to look at code that already exists and ask **is this the shape the engine
should have?** - and how to review a change (section 7). A hunt for defects
finds defects while the design goes wrong underneath; nobody hunting bugs in a
file concludes that the file should not exist.

---

## Absolutes

- **Report what you find, even when nobody asked** (section 6).
- **Never silently redesign something you were asked to fix.** Name it, cost it,
  let the owner decide.
- **Report the disconfirmations too.** "I expected X and the source says
  otherwise" is what makes the confirmed findings trustworthy.

---

## 1. The tells

Every one of these is the same failure: **the code stopped expressing the design
and started depending on someone remembering it.**

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
| Something kept for a user           | A shape the engine would not choose today |

### 1.1 An invariant living in a comment

Prose like *"Must precede scene I/O"* says there is a rule the code does not
hold. The fix is rarely a better comment: it is a type, a constructor that cannot
be called out of order, or one shared sequence with a single home (`bootHost` in
`tools/project_boot.h` for the hosts' prologue). Some constraints are too small
to be worth a type; acting is a judgment.

### 1.2 A field per case

When something new must survive an operation and the answer is another member,
the mechanism is accumulating rather than generalising: fields added one
incident at a time, each with its own rule. Ask: would one more case be
absorbed, or grow it again?

### 1.3 A guard instead of a state

A check every caller must remember - "refuse this during play" asked by each
save path - means the new operation that forgets is a bug. A state the code can
be *in* beats a question every caller must remember to ask.

### 1.4 One fact stated twice

The defect this engine's reviews find most. A fact with two owners - a constant
mirrored in GLSL, a rule re-implemented in a second file, a comment and the code
it describes, a doc page and the struct it lists - is correct only until one of
them changes, and the second copy is usually right the day it is written.

Ask what *else* states a fact before asking whether this copy is wrong. The fix
is one owner: a shared constant, a generated list, a helper both call, or a
check that fails the suite when the two disagree (`tests/docs/docs_tests.cpp`
for docs against source).

### 1.5 One concept, several mechanisms

Count the ways the engine does one thing. Each of several persistence paths can
be right while the sum is unmaintainable: a fix in one never reaches the
others.

### 1.6 A class holding unrelated state

Read a class's members as a list and ask whether they are one idea. Members that
each change for reasons unrelated to the others - which file is open, what a
widget shows, what Play captured - are a container, not a design.

### 1.7 Repeated fixes in one place

The cheapest signal, because it is history rather than opinion:

    git log --since=6.months --format=%x01%s --name-only -- src |
        awk '/^\x01fix/{f=1;next} /^\x01/{f=0} f&&/\.(cpp|h)$/{print}' |
        sort | uniq -c | sort -rn

A file that moved counts under each of its names; add them. Read a file's last
five fixes together. If each adds a case rather than removing the reason for
cases, the design is the defect and the fixes are interest. If the fixes
*removed* cases, the rank is not a verdict.

### 1.8 A comment that argues

A comment defending a decision is doing a commit message's job, and usually
sits where the author knew the design was questionable. Treat it as a pointer to
the thing being justified
([code-style.md](code-style.md#61-what-bounds-a-comment)).

### 1.9 Something kept for a user

An alias, an overload, a fallback, a tolerated old spelling or a comment saying
"for X" that exists because a game, an example or an old file relies on it. The
engine would not have it if it were designed today
([design.md](design.md#12-the-engine-is-designed-for-itself)).
Report it with its users; the fix is to delete it and port them
([implementation.md](implementation.md#7-changing-a-shape-its-users-rely-on)).

---

## 2. What is not a tell

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
   capture. A number kills a bad hypothesis in a minute.
2. **Read the fixes, not just the code.** A file's last five commits say more
   about its design than the file does.
3. **Check a claim the code makes about itself.** A comment saying what a
   function does, a `@param` naming a parameter, a doc listing a struct's fields,
   a count in a guide - grep each against the source.
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

**Does this thing make the next thing easier, or harder?** A **smoothing**
design absorbs the next case without changing shape; a **suffocating** one makes
everything work around it.

**Do people extend it, or route around it?** Count the callers that bypass it,
duplicate a piece of it, or reach past it to what it wraps: a second path to the
same outcome, a caller reconstructing internals, a helper that exists to avoid
it, a comment saying "we do it this way here because X does not handle Y."
Three bypasses mean suffocating however clean it looks - usually reasonable code
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

- **Surface it unprompted.**
- **Lead with the evidence** - `file:line`, the churn count, the four fields
  added one at a time. A structural claim without evidence is taste.
- **Separate confirmed from suspected,** and say which is which.
- **Report the disconfirmations.**
- **Give the cost both ways** - what the redesign takes, and what leaving it
  costs. The decision is the owner's; the estimate is yours.

---

## 7. Reviewing a change

**Approve a change when it leaves the engine better than it found it**, even if
it is not how you would have written it.

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
mechanisms come from.

**A branch is judged at its last commit.** The tip must build and pass every
suite; the commits before it need not each build, and nobody spends a machine's
hours proving they do. Polishing history - reordering, folding fixes into the
commits they belong to - is checked by the tip's tree matching the one that
was tested.

Say which of your comments block and which are suggestions. A defect, a broken
absolute, an unmeasured performance claim, docs that did not ship and a
half-finished refactor block. Everything in section 2 does not.
