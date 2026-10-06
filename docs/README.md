# vkmEngine documentation

Two audiences, and they want different things. Find yourself here first.

## Making a game with the engine

Start at **[getting-started.md](getting-started.md)** - install, first project,
writing a behavior, and shipping. Then whichever of these you need:

| I want to | Read |
|---|---|
| put things in a world and move them | [ecs.md](reference/ecs.md) |
| write gameplay code | [scripting.md](reference/scripting.md) |
| read a key, a button, or the mouse | [input.md](reference/input.md) |
| build a HUD or a menu | [ui.md](reference/ui.md) |
| make things fall, collide, or walk | [physics.md](reference/physics.md) |
| play a sound | [audio.md](reference/audio.md) |
| play an animation | [animation.md](reference/animation.md) |
| let two people play together | [networking.md](reference/networking.md) |
| load a model or a texture | [io.md](reference/io.md) |
| author a scene by hand | [editor.md](reference/editor.md) |

Each of those pages but io.md opens with what you write and ends with a **How
it works inside** section. You can stop at that heading; it is there for whoever
maintains the engine, not for whoever uses it.

## Working on the engine itself

`guides/` is the law and `reference/` is the map. The first time:

1. [reference/architecture.md](reference/architecture.md) - what the engine is,
   how a project reaches it, the core model, stage order, then the detail.
2. The guides, in the order [guides/README.md](guides/README.md) gives - they
   are what makes a change look like it was always there. Read engine.md's
   settled table before proposing anything structural.
3. The subsystem page for whatever you are touching, under `reference/`.

Then [building.md](reference/building.md) for targets and flags, and
[threading.md](reference/threading.md), [resources.md](reference/resources.md),
[rendering.md](reference/rendering.md),
[lighting.md](reference/lighting.md),
[visibility.md](reference/visibility.md),
[hierarchy.md](reference/hierarchy.md) and
[events.md](reference/events.md) for the parts a game never calls
directly.

## The rule these pages are kept by

**The code is the source of truth.** If a page disagrees with the source, the
source is right and the page is a defect - fix it or say so. The checks in
`tests/docs/docs_tests.cpp` fail the test suite on the ways that have actually
rotted: a dead link or anchor, a named source file that no longer exists, a
hand-copied list that stopped matching the code it describes, and a page no index
links to.
