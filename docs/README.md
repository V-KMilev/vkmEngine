# vkmEngine documentation

Two audiences: people making a game with the engine, and people working on the
engine itself.

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
| shape the ground, paint it, grow grass on it | [terrain.md](reference/terrain.md) |
| play a sound | [audio.md](reference/audio.md) |
| play an animation | [animation.md](reference/animation.md) |
| let two people play together | [networking.md](reference/networking.md) |
| react to something that happened | [events.md](reference/events.md) |
| make or find an asset in code | [resources.md](reference/resources.md) |
| load a model or a texture | [resources.md](reference/resources.md#importing-a-model) |
| author a scene by hand | [editor.md](reference/editor.md) |

Each page opens with what you write and closes with a **How it works inside**
section (then its key files) for whoever maintains the engine; you can stop there.

## Working on the engine itself

`guides/` is the law and `reference/` is the map. The first time:

1. [reference/architecture.md](reference/architecture.md) - the map: what the
   engine is, the frame and its stages, where the code lives.
2. The guides, in the order [guides/README.md](guides/README.md) gives. Read
   engine.md's settled table before proposing anything structural.
3. The subsystem page for whatever you are touching, under `reference/`.

Then [building.md](reference/building.md) for targets and flags, and
[threading.md](reference/threading.md),
[rendering.md](reference/rendering.md),
[lighting.md](reference/lighting.md),
[visibility.md](reference/visibility.md) and
[hierarchy.md](reference/hierarchy.md) for the parts a game never calls
directly.

## The rule these pages are kept by

**The code is the source of truth.** If a page disagrees with the source, the
page is the defect - fix it or say so. `tests/docs/docs_tests.cpp` fails on a
dead link or anchor, a name that does not exist, a copied list that stopped
matching the code, and a page no index links to. What a page should hold is
[code-style.md](guides/code-style.md#64-the-manual).
