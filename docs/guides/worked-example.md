# A Worked Judgment

The other guides are rules, and rules underdetermine taste. This is one complete
judgment made end to end - the evidence, what each guide says about it, the
verdict, and just as importantly the places where the honest answer was *leave it
alone.*

**The verdict is not the point; the reasoning is.** These are snapshots of real
code and will age. If the file has changed, follow the method and reach your own
answer.

---

## The subject

`src/editor/framework/scene_io_controller.h` and its `.cpp` - 871 lines
together - which own saving, loading, opening a project's scene, and the play
session that Play captures and Stop restores.

## 1. The evidence, measured before judged

    15        public methods, past the ctor and the deleted Rule of 5
    9         fixes in six months, the fourth-highest in the engine
    m_currentScenePath        which file is open
    m_saveAsBuffer[256]       what a dialog widget is showing
    m_openSaveAsPopup         whether that dialog is up
    m_playSnapshot            the scene, serialized to a string
    m_playAssets              the asset list, serialized to a string
    m_playSnapshotDirty       whether it needs recapturing
    m_playSnapshotHistory     an undo-history marker
    refusedDuringPlay()       a private question the two save paths remember to ask

Nothing above is an opinion. That matters: the argument that follows can be
checked, and a structural claim without evidence is taste
([review.md](review.md#6-reporting-what-you-find)).

## 2. What each guide says

**[review.md](review.md#12-a-field-per-case) - a field per case.** Four members
hold one concept, "what Play captured," and they arrived one incident at a time:
the scene, then the assets, then a dirty flag, then a history marker. The
mechanism is not generalising, it is accumulating. The fifth thing that must
survive Stop will be a fifth field.

**[review.md](review.md#13-a-guard-instead-of-a-state) - a guard instead of a
state.** `refusedDuringPlay()` is a state machine nobody wrote down, and the
state it guards is not modelled - `isPlaying()` is `!m_playSnapshot.empty()`, so
four call sites in four files each re-derive "a session is live" from a buffer
holding text. Every new operation must remember to ask, and the one that forgets
is a bug an author finds, not a compiler.

**[review.md](review.md#15-a-class-holding-unrelated-state) - unrelated state.**
Read the members as a list. Which file is open, what a widget is showing, and
what Play captured are three ideas. Each changes for reasons unrelated to the
other two, which is why the file is fixed so often.

**[review.md](review.md#4-suffocating-or-smoothing) - the route-around test.**
The decisive one. Play-session concerns reach into a class whose other half is
file dialogs; anything new touching play mode either grows this class or works
around it. It suffocates rather than smooths.

**[engine.md](engine.md#2-what-it-optimises-for) - the value order.** Correctness
that survives load is first. Nine fixes say it is not surviving.

**[implementation.md](implementation.md#61-tests-you-can-actually-run) - the
derived-state test.** `m_playSnapshotDirty` and `m_playSnapshotHistory` exist to
track whether the two strings are current. State kept in sync by hand, which is a
bug that reproduces only in sequence.

## 3. Verdict

**Suffocating. Redesign, not patch** -
[review.md](review.md#5-patch-or-redesign) - because the cost of the next change
is rising: each fix has added a case rather than removed the reason for cases.

Not because the code is ugly. It is reasonable code holding a responsibility that
turned out to be the wrong one to centralise.

## 4. What the fix is, and what it is not

**A redesign is not a rewrite.** The smallest change that removes the reason the
fixes keep coming:

- A play session becomes **one thing** that owns capture and restore, so the
  next thing that must survive Stop is a field on *it* rather than a fifth field
  here - or better, is captured by construction and needs no field.
- The editor's play state becomes **a state the code is in**, so an operation
  that must not run during play cannot, instead of remembering to ask.
- The dialog buffer and popup flag go where widget state goes.

What it is **not**: rewriting scene I/O, redesigning serialization, or touching
anything outside the reason for the churn. And per
[README.md](README.md#decide-or-ask), this is not a call to make inside a task -
redesigning something you were asked to fix is named, costed, and handed to the
owner.

---

## 5. Two places the honest answer was "leave it alone"

Calibration cuts both ways. A guide that only ever finds fault trains you to find
fault.

### 5.1 The 62-line comment on `AudioDevice`

`src/engine/system/audio/audio_device.h:45-106` carries a Doxygen block many
times longer than anything else in the file. When this judgment was made it also
carried four ALL-CAPS headings and one arguing clause. The instinct is to cut it
to a brief and a paragraph.

**Mostly wrong - and the part that is right has nothing to do with the length.**
Run the bound in [code-style.md](code-style.md#61-what-bounds-a-comment) over it
line by line: can a caller work this out for themselves?

**Stays, all of it.** That no device is a normal state, so `open()` returning
false leaves every other call a no-op rather than a failure. That the whole
class, `render()` included, is main-thread only and nothing in it is guarded, so
a harness lending the mixer a thread must join that thread before `close()`. That
a voice plays a clip's samples in place and shares ownership of them, which is
what lets a sound outlive the graph it came from instead of reading freed memory.
And the two ThreadSanitizer races in vendored miniaudio, the third that fires
only while something reads a playback cursor, and why all three are left alone.
A caller infers none of that from `bool open()` and `void render()`, nothing else
in the tree records it, and moving it to `docs/reference/system/audio.md` files
it where the person about to call `render()` from a worker thread is not looking.
The length was never the defect. This is what a seam's block is *for*.

**Went.** The four ALL-CAPS headings - `NO DEVICE IS A NORMAL STATE`, `WHICH
THREAD MAY CALL THIS`, `WHAT THE SEAM IS, EXACTLY`, `THE KNOWN RACES ARE
MINIAUDIO'S`. The paragraphs under them say the same things without them, and
headings are the structure of a document: a block that genuinely needs them is
telling you one of its topics belongs in `docs/reference/`. And the arguing
clause that opened the ownership paragraph - *"worth stating, because it is what
the design is"* - which defended the paragraph to a reviewer rather than telling
a caller anything. The clause went, the paragraph stayed.

That was an edit of six lines, and what it left is the block the file carries
now: sixty-two lines, still long and still correct. An agent working to a line
budget would instead have deleted the only written record of a threading
contract and three upstream races, which is much the more expensive direction to
be wrong in.

### 5.2 Behavior fields breaking the member rule

A `Behavior` subclass declares its authored fields as bare public members on a
class - `Spinner::degreesPerSecond`, and 22 more like it in
`examples/stress_arena/src/stress_arena.h` - which violates
[code-style.md](code-style.md#41-the-structclass-member-rule) head-on, and does
it at a scale that makes it look like drift rather than a decision.

**Leave it.** It is a documented exception with a stated reason: the field name
*is* the serialized identity, appearing in the scene JSON and as the inspector's
label, so an `m_` prefix would leak into both. Runtime-only state on the same
classes does take `m_`, which is the tell that the shape is deliberate. The rule
has an exception list for exactly this, and "the guide says X" is not a finding
when the guide also says why this is not X.

---

## 6. What this is meant to calibrate

- **Measure first.** Every claim in section 1 is checkable. The judgment is only
  as good as that list.
- **A finding is a shape, not a smell.** "Feels bloated" is nothing. "Four fields
  holding one concept, added one at a time" is a finding.
- **The verdict names the smallest change** that removes the cause, not the best
  code you could imagine writing.
- **Restraint is a result.** Sections 5.1 and 5.2 are as much the point as
  section 3. Two of the loudest style violations in the engine are mostly or
  entirely correct, and an agent that "fixed" them wholesale would have destroyed
  knowledge and churned a type for nothing. Note what 5.1 keeps: the answer was
  six lines, not zero and not the whole block.
- **Know whose call it is.** The analysis is yours. The decision to act on it,
  here, is not.
