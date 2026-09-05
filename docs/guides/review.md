# Review Guide

How to look at code that already exists and tell whether the design is drifting.

The other guides assume you are adding something. This one assumes it is already
there and asks a different question: **is this the shape the engine should
have?**

That needs its own guide because it cannot be answered from inside a task. A
review framed as "find the defects" will find defects, and every one can be real
while the design quietly goes wrong underneath. Nobody hunting bugs in a file
ever concludes that the file should not exist.

The examples are from this engine and were true when written. If one disagrees
with the source, the source is right, and the drift is worth saying out loud.

---

## Absolutes

- **Report what you find, even when nobody asked.** Drift nobody hears about is
  drift that continues. Section 6.
- **Never silently redesign something you were asked to fix.** Name it, say what
  it would cost, and let the owner decide.
- **Report the disconfirmations too.** "I expected X and the numbers say
  otherwise" is what makes the confirmed findings trustworthy.

---

## 1. The tells

Every one of these is the same failure in different clothes: **the code stopped
expressing the design and started depending on someone remembering it.** If you
remember one sentence from this guide, that is the one - the rest is derivable.

| Tell                                | What it means                            |
|-------------------------------------|------------------------------------------|
| An invariant living in a comment    | The structure refuses to encode a rule    |
| A field per case                    | Fixes accreting where a mechanism belongs |
| A guard instead of a state          | A state that was never modelled           |
| One concept, several mechanisms     | Nobody chose, so everybody chose          |
| A class holding unrelated state     | A container that grew instead of a design |
| The same file fixed again and again | The design is telling you something       |
| A comment that argues               | A decision defended instead of expressed  |

### 1.1 An invariant living in a comment

The clearest tell in the list. Look for prose of this shape:

    "Must precede scene I/O."
    "in the one order that works"
    "Declared before the Engine so it outlives it"

Each says: there is a rule, the code does not hold it, and the next person is
expected to read this and be careful. Nothing checks it. Swap two lines and the
compiler is silent. In `app/editor/main.cpp`, `app/runtime/main.cpp` and
`app/cooker/main.cpp` the boot sequence is hand-ordered in three places with the
constraints written as comments, and the editor and runtime already order two of
those steps differently. Both work. Neither works *because* of anything.

The fix is almost never a better comment. It is a type, a constructor that cannot
be called out of order, or one shared sequence with a single home.

### 1.2 A field per case

When something new must survive an operation and the answer is another member,
the mechanism is not generalising, it is accumulating. `SceneIOController`
records a play session as `m_playSnapshot`, then `m_playAssets`, then
`m_playSnapshotDirty`, then `m_playSnapshotHistory`. Four fields, four rules, one
incident at a time. The fifth thing that must survive Stop will be a fifth field.

Ask: if one more case arrived tomorrow, would this absorb it or grow again?

### 1.3 A guard instead of a state

`refusedDuringPlay()` is the whole state machine for "this operation must not run
during play": a private question the two save paths remember to ask
(`scene_io_controller.cpp`, ). Every new operation has to remember it
too, and the one that forgets is a bug nobody sees until an author hits it. The
state it guards is not modelled either. `isPlaying()`
(`scene_io_controller.h`) is `!m_playSnapshot.empty()`, so "a session is
live" is a side effect of a buffer holding text, and the four call sites in four
files each re-derive it from that.

A state the code can be *in* beats a question every caller must remember to ask.

### 1.4 One concept, several mechanisms

Count the ways the engine does one thing. If persistence happens through a
serializer here, a hand-rolled dump there and a third path in a tool, no single
one is wrong and the sum is unmaintainable. This is what makes a codebase feel
larger than it is: the reader must learn each variant, and a fix in one never
reaches the others.

### 1.5 A class holding unrelated state

Read a class's members as a list and ask whether they belong to one idea.
`SceneIOController` holds the current scene path, a `char[256]` save-as dialog
buffer with its popup flag, and the whole play-session snapshot. Three
responsibilities: which file is open, what a widget is showing, and what Play
captured. Each changes for reasons unrelated to the others, which is exactly why
it is fixed so often.

### 1.6 Repeated fixes in one place

The cheapest signal available and the most honest, because it is history rather
than opinion:

    git log --since=6.months --format=%x01%s --name-only -- src |
        awk '/^\x01fix/{f=1;next} /^\x01/{f=0} f&&/\.(cpp|h)$/{print}' |
        sort | uniq -c | sort -rn

A file that keeps needing fixes is not unlucky. Read its last five fixes
together: if each adds a case rather than removing the reason for cases, the
design is the defect and the fixes are interest payments.

### 1.7 A comment that argues

A comment defending a decision against an imagined objection is doing a commit
message's job. It usually appears where the author knew the design was
questionable and wrote the defence instead of the fix. Treat a paragraph of
justification as a pointer to the thing being justified. How to write the
replacement is [code-style.md](code-style.md#61-what-bounds-a-comment); what
concerns you here is only what it points at.

---

## 2. What is not a tell

Being wrong in this direction wastes real work. Hold these firmly.

- **Comment volume.** Measured on this engine: the twenty most-fixed files sit
  *below* the engine-wide comment density. Bloated comments are a writing problem
  with their own fix
  ([code-style.md](code-style.md#61-what-bounds-a-comment)), not evidence of
  a bad design. Sections 1.1 and 1.7 are about what a comment *says*, never how
  many there are.
- **File size alone.** A long file of one kind of thing is fine. A short file of
  three kinds is not.
- **A long straight-line function.** Boot sequences and draw calls are long
  because the work is long. Length matters when it hides branching, not when it
  is a list.
- **A wide interface.** Twenty methods that are all the same kind of operation is
  an interface. Six across three responsibilities is a god object. Count
  *responsibilities*.
- **Code you would have written differently.** Taste is not drift.

---

## 3. How to look

1. **Measure before judging.** Churn, size, density, counts. A number kills a bad
   hypothesis in a minute and makes a good one undeniable.
2. **Read the fixes, not just the code.** The last five commits to a file say
   more about its design than the file does.
3. **Strike the comment and reread.** If a competent reader would now ask a
   question, the answer to that question is the only comment needed. If the code
   becomes *unreadable*, the comment was holding up a design that cannot stand.
4. **Read fifteen files cold,** as someone new to the engine. Drift is obvious
   from outside a task and invisible from inside one.
5. **Use the thing.** Press Play, Pause, Stop, open a project, save it, cook it.
   Most defects in this engine were found by an owner using it and almost none by
   agents reading it.

---

## 4. Suffocating or smoothing

Section 1 reads history: fixes that accreted, fields that piled up. That is
backward-looking, and by the time it is clear the design is already load-bearing.
This is the forward-looking half, and it is the question that matters most:
**does this thing make the next thing easier, or harder?**

A **smoothing** design absorbs the next case without changing shape. A
**suffocating** one makes every future thing work around it.

The tell is visible in the code rather than in your judgment of it:

> **Do people extend it, or route around it?** Count the callers that bypass the
> thing, duplicate a piece of it, or reach past it to what it wraps. A design
> nobody routes around is doing its job however ugly it looks. A design with
> three bypasses is suffocating however clean it looks.

That gives you something to grep for instead of a feeling to have. Look for a
second path to the same outcome, a caller that reconstructs internals rather than
asking, a helper that exists only to avoid using the thing, and a comment saying
"we do it this way here because X does not handle Y."

The related forward question, for something you are about to build: **if a
hundred things depend on this, is it still the right shape - or does it become
the thing everything works around?** Suffocating designs are rarely bad code.
They are usually reasonable code holding a responsibility that turned out to be
the wrong one to centralise.

---

## 5. Patch or redesign

Most findings are not worth acting on today. The ones that are share a shape.

**Redesign when the cost of the next change is rising.** If each fix in an area
has been larger than the last, or has added a case rather than removed the reason
for cases, patching is now the expensive option.

**Patch when the thing is done and merely imperfect** - it works, it is not
growing, and nothing new is being built on it.

Two rules that matter more than the judgment:

- **A redesign is not a rewrite.** Take the smallest change that removes the
  reason the fixes keep coming, and leave the rest alone.
- **Say it even when the answer is "not now."** A named, deferred problem is a
  decision. An unnamed one is a surprise later.

---

## 6. Reporting what you find

On a codebase too large to hold in one head, reporting is a duty rather than a
courtesy.

- **Surface it unprompted.** The owner should not have to suspect a problem to be
  told about it. A review that only answers the question asked will keep missing
  the ones nobody knew to ask.
- **Lead with the evidence.** `file:line`, the churn count, the four fields added
  one at a time. A structural claim without evidence is taste.
- **Separate confirmed from suspected,** and say which is which.
- **Report the disconfirmations.** They are as useful as findings and they are
  what keeps the findings credible.
- **Give the cost both ways.** What the redesign takes, and what it costs to
  leave alone. The decision is the owner's; the estimate is yours.
