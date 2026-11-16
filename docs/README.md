# vkmEngine documentation

Two audiences: people making a game with the engine, and people working on the
engine itself.

## Making a game with the engine

Start at **[getting-started.md](getting-started.md)** - install, first project,
writing a behavior, and shipping. **[recipes.md](recipes.md)** has the common
jobs of a game, each in a few lines. Then whichever of these you need:

| I want to | Read |
|---|---|
| put things in a world and move them | [ecs.md](reference/ecs.md) |
| write gameplay code | [scripting.md](reference/scripting.md) |
| read a key, a button, or the mouse | [input.md](reference/input.md) |
| build a HUD or a menu | [ui.md](reference/ui.md) |
| make things fall, collide, or walk | [physics.md](reference/physics.md) |
| float something on water, or ask where the water is | [water.md](reference/water.md) |
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

The first time:

1. [reference/architecture.md](reference/architecture.md) - the map: what the
   engine is, the frame and its stages, where the code lives, and how new code is
   shaped.
2. [reference/decisions.md](reference/decisions.md) - what the engine holds to and
   refuses, and what has been settled. Read its table before proposing anything
   structural.
3. The subsystem page for whatever you are touching, under `reference/`.

Then [building.md](reference/building.md) for targets and flags, and
[threading.md](reference/threading.md),
[rendering.md](reference/rendering.md),
[lighting.md](reference/lighting.md),
[visibility.md](reference/visibility.md) and
[hierarchy.md](reference/hierarchy.md) for the parts a game never calls
directly. [features.md](reference/features.md) lists what the engine has, area by
area, with each feature's status, and [targets.md](reference/targets.md) says what
"done" means: the frame budgets, how long a common job may be, and what the editor and
the code are held to.

## The rule these pages are kept by

**The code is the source of truth.** If a page disagrees with the source, the
page is the defect - fix it or say so. The docs suite, `tests/docs/check_docs.py`,
fails on a name a page gives that the tree does not hold (below), a third-party
module `building.md` does not justify, a platform named beside Linux and Windows,
and a page no index links to; what it holds the code to is
[building.md](reference/building.md#tests)'s. It reads no sentence.

A page explains how a subsystem works - its idea and its concepts; the code holds the
detail - fields, steps, constants, edge cases - and a page names the file rather than
copying it. Its reader is most often an agent working in this repo with a limited
context: a line it does not need costs it, and every sentence is one more thing to keep
true.

- **What it is, how to use it, what it promises.** A paragraph on what the subsystem is
  for; the calls a project or the engine makes, with an example only where the use is not
  obvious; the contracts and limits a caller must respect; and the one non-obvious *why*
  that stops a reader "fixing" it wrong, in a sentence or two.
- **No measurements, no hardware.** A millisecond, a percentage or a frame rate is true of
  one machine on one day. "Cheap enough to run every frame" is what a reader needs; the
  number goes in the commit. A number stays only when it is a contract - a capacity, a
  default, a format's limit - or the one that decided a row of decisions.md.
- **No extremes and no derivations.** One plain example, not the edge case that took a week;
  the conclusion, not the working that reached it.
- **Not the header again.** A field's meaning lives on the field. The page says how the pieces
  fit, and names the header for the rest.
- **One fact, one page.** Where another page owns it, link.
- **No history.** Not what a thing used to be, which release built it, what was tried, or what
  a bug did before it was fixed. Restate the constraint in the present - "`playing` is session
  state; saved, a finished one-shot would restart on load". Where the history is a decision -
  built and reverted, measured and declined - it is a row in
  [decisions.md](reference/decisions.md#what-has-been-decided), the one place history is kept,
  and the page states the outcome.
- **No line numbers.** Cite a file and a symbol - `ecs/scene.h`, `Scene::add`. The first edit
  above a cited line makes the number lie.
- **What it names exists.** The suite checks a relative link, an anchor, a backticked path to a
  code file or a page, and a backticked qualified name of a scope the tree declares. Any other
  name is the writer's to check.
