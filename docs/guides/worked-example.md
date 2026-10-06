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

`src/editor/session/scene_io_controller.h` and its `.cpp` - 871 lines together
when this was written - which own saving, loading, opening a project's scene, and
the play session that Play captures and Stop restores.

## 1. The evidence, measured before judged

This is the file as it stood when the judgment was made. Section 5 says what
happened next; read the argument first, because the argument is the thing being
taught.

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

**[review.md](review.md#16-a-class-holding-unrelated-state) - unrelated state.**
Read the members as a list. Which file is open, what a widget is showing, and
what Play captured are three ideas. Each changes for reasons unrelated to the
other two, which is why the file is fixed so often.

**[review.md](review.md#4-suffocating-or-smoothing) - the route-around test.**
The decisive one. Play-session concerns reach into a class whose other half is
file dialogs; anything new touching play mode either grows this class or works
around it. It suffocates rather than smooths.

**[engine.md](engine.md#2-what-it-optimises-for) - the value order.** Correctness
that survives load is first. Nine fixes say it is not surviving.

**[implementation.md](implementation.md#9-checks-with-an-answer) - the
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

## 5. What actually happened

The verdict was handed to the owner, and two of the three changes in section 4
were made. Saying which, and which was not, is the part a stale guide would have
lost.

**Taken.** The four snapshot fields are one `PlaySnapshot m_play`
(`session/play_snapshot.h`), a type whose whole subject is the world as it
stood when Play was pressed - so the asset list that arrived later is a member of
*it*, and the fifth thing to survive Stop will be too. That type also turned out
to be testable on its own, which the four fields never were: `tests/editor/play_tests.cpp`
drives capture and restore with no window, because a snapshot needs a scene and a
resource manager and nothing else. The fifth thing did arrive: whether a session
is ejected, and the cursor its game had, sat on the controller as two more
fields reset by hand where a session ended, until they joined the snapshot,
whose release() is that end.

**Taken, in half.** `isPlaying()` is `m_play.held()` now rather than
`!m_playSnapshot.empty()`, so a session being live is a state something owns
instead of a side effect of a string's length. But `refusedDuringPlay()` is still
there and still a question the save paths remember to ask. Modelling the state
made the question answerable; it did not remove the need to ask it.

**Not taken.** The dialog buffer and popup flag are still on the controller, and
a file picker has joined them. So [review.md](review.md#16-a-class-holding-unrelated-state)'s
third tell stands where the other two moved.

Two things worth taking from that. Fixing one tell does not fix its neighbours -
they were three findings, not one, and only the ones acted on changed. And the
change that paid was **not** the smallest one available: four fields moving into
a new type is more code than a fifth field would have been, and it is the only
one of the two that stopped the bleeding. That is the tension
[design.md](design.md#23-what-is-the-smallest-change-that-fits) names, resolved
in the direction it says to resolve it.

## 6. Two places the honest answer was "leave it alone"

Calibration cuts both ways. A guide that only ever finds fault trains you to find
fault.

### 6.1 The 62-line comment on `AudioDevice`

`src/engine/system/audio/audio_device.h` carried a Doxygen block many times
longer than anything else in the file, with four ALL-CAPS headings and one
arguing clause. The instinct is to cut it to a brief and a paragraph.

**Mostly wrong - and the part that is right has nothing to do with the length.**
Run the bound in [code-style.md](code-style.md#61-what-bounds-a-comment) over it
line by line: can a caller work this out for themselves? For most of it, no.
That no device is a normal state, so `open()` returning false leaves every other
call a no-op rather than a failure. That the whole class, `render()` included, is
main-thread only and nothing in it is guarded. That a voice plays a clip's
samples in place and shares ownership of them, which is what lets a sound outlive
the graph it came from instead of reading freed memory. A caller infers none of
that from `bool open()` and `void render()`, and an agent working to a line
budget would have deleted the only written record of a threading contract and
three upstream races.

**Went, and rightly.** The four ALL-CAPS headings - `NO DEVICE IS A NORMAL
STATE`, `WHICH THREAD MAY CALL THIS`, `WHAT THE SEAM IS, EXACTLY`, `THE KNOWN
RACES ARE MINIAUDIO'S`. The paragraphs under them say the same things without
them. And the arguing clause that opened the ownership paragraph - *"worth
stating, because it is what the design is"* - which defended the paragraph to a
reviewer rather than telling a caller anything.

**And the verdict was only half right.** It said "stays, all of it", as if the
only alternative to sixty-two lines were cutting them. There was a third option,
and [implementation.md](implementation.md#8-a-long-comment-is-a-diagnosis) case
2 names it: material about the subsystem rather than one call belongs in
`docs/reference/` with a pointer left behind. The ALL-CAPS headings were the
tell. The block is a third of its length now; the thread rules and the three
upstream races are two named sections of `docs/reference/audio.md`, and
the block's last lines point at them. Nothing a caller needs was lost.

The lesson was never "long blocks stay". It is **do not delete knowledge to hit a
number** - and moving it is not deleting it.

### 6.2 Behavior fields breaking the member rule

A `Behavior` subclass declares its authored fields as bare public members on a
class - `Spinner::degreesPerSecond`, and dozens more across the examples - which
violates
[code-style.md](code-style.md#41-the-structclass-member-rule) head-on, and does
it at a scale that makes it look like drift rather than a decision.

**Leave it.** It is a documented exception with a stated reason: the field name
*is* the serialized identity, appearing in the scene JSON and as the inspector's
label, so an `m_` prefix would leak into both. Runtime-only state on the same
classes does take `m_`, which is the tell that the shape is deliberate. The rule
has an exception list for exactly this, and "the guide says X" is not a finding
when the guide also says why this is not X.

---

## 7. What this is meant to calibrate

- **Measure first.** Every claim in section 1 is checkable. The judgment is only
  as good as that list.
- **A finding is a shape, not a smell.** "Feels bloated" is nothing. "Four fields
  holding one concept, added one at a time" is a finding.
- **The verdict names the smallest change that removes the cause, which is not
  the smallest diff.** Four fields into one type was more code than a fifth field,
  and the only change that stopped the fixes
  ([design.md](design.md#23-what-is-the-smallest-change-that-fits)).
- **Restraint is a result.** Sections 6.1 and 6.2 are as much the point as
  section 3. Two of the loudest style violations in the engine are mostly or
  entirely correct, and an agent that "fixed" them wholesale would have destroyed
  knowledge and churned a type for nothing. Note what 6.1 keeps: every constraint
  a caller could not infer, whether it stayed in the block or moved to a page
  the block now names.
- **A verdict is not the end of it.** Sections 5 and 6.1 are both this guide
  being wrong in a useful direction: one recommendation was taken and improved
  on, one was not taken at all, and one verdict - "stays, all of it" - missed
  the option another guide had already written down. Check the file before you
  trust the argument about it.
- **Know whose call it is.** The analysis is yours. The decision to act on it,
  here, is not.
