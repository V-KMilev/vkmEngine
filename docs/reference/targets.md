# Targets

What "done" means, as things that can be checked. An area that meets this page is finished, so
stop and move on. The owner has the last word, judging against it.

## What the engine is for

Almost any type of game, but not absolutely everything. It handles the common things really well,
and a missing feature is fine until someone asks for it.

## A frame

| Machine | Resolution | Tier | Goal |
|---|---|---|---|
| FX-8320, RTX A4000 | 2560x1440 | Max | 500 fps |
| 7800X3D, RTX 5080 | 2560x1440 | Max | 700 fps |

A frame rate only counts if the frames are steady:

- The slowest 1% of frames average no more than 1.5 times the average frame.
- In a 20 second run, no frame takes over twice the median.
- Nothing stalls the first time it is seen: a shader, a material, a light.

`vkm bench examples/the_lab` times the lab's shipping build on the machine it runs on, at each
tier and from each view its `bench.json` lists: spawn, overview, sea, gallery and night hold the
camera still, and walk and turn move it. Each view runs the whole game; only the camera, and for
night the sun, is set. It fails when a view's average frame or the average of its slowest 1% is
over that tier's `averageMs` or `low1Ms` in `bench.json`. Those budgets are the project's, not the
goals above, and no steadiness rule is checked.

A feature a scene does not use costs its frames nothing: no system walks for it, no pass draws for
it, nothing is dispatched for it.

A gain that loses quality is listed in [decisions.md](decisions.md), with what it bought.

## Making a game

- `vkm new`, then `vkm run`, plays a game in under a minute, with nothing to explain.
- What is saved in the editor is what `vkm run` plays.
- A changed behavior is running again within 3 seconds.
- A common job takes 10 lines or fewer.
- A new behavior field shows in the inspector with no other code.

## The editor

- One accent color and three that mean something: success, warning, error. Axis colors stay.
- Nothing looks like a library's defaults: chevrons, close buttons, splitters and rows are the
  editor's own.
- A new component's inspector is one table of rows, and saving and loading it need no code of its own.

## The code

- A file holds one job, so good design keeps files small. The docs suite reports each area's code
  lines and lists any file under `src/` or `app/`, the code a review reads, past 1,500 lines; it
  fails nothing. A test or an example splits only where a split reads better.
- Everything documented at the release tag stays documented, and no comment restates its code. A
  long explanation belongs on one of these pages.
- Every third-party module is justified in [building.md](building.md#external-modules).
- GCC, Clang and the shipping engine build with no warnings, and every suite passes on them.
