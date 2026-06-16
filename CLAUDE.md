# CLAUDE.md

Guidance for Claude Code (and any agent) working in this repository.

**The full operating manual lives in [`claude_helper/`](claude_helper/). Read
[`claude_helper/README.md`](claude_helper/README.md) before working on the code** -
it gives the pre-flight order (orient -> how to fit the change -> the quality bar ->
the mechanics -> the subsystem doc) and the working loop. This file is only the
always-loaded summary; the helper is the source of truth.

## Build and run

```bash
git submodule update --init --recursive
cmake -B build -G Ninja
cmake --build build
./build/bin/engine
```

CMake 3.25+, Ninja, C++17, OpenGL 4.3 core.

## The load-bearing invariants

These are the seams that keep the engine replaceable and fast. Don't cross them
without surfacing it first (full detail in
[claude_helper/guides/development.md](claude_helper/guides/development.md)):

- **Engine never reaches into the backend.** All engine-to-GPU traffic goes through
  the `RenderBackend` interface; never include a `gl_*` header from engine code.
  `MaterialAsset` is the renderer contract.
- **`Engine` is stack-constructible, not a singleton** (no `Engine::get()`). The
  only singleton is `ThreadPool::get()`.
- **`Scene` is an open, type-erased ECS.** Any plain struct is a component; never
  edit `Scene` to "add support" for one. Components are data; behavior lives in
  systems.
- **Systems communicate through `FrameContext` and components, not each other**,
  and each runs at exactly one `SystemStage` (Input, Simulation, Transform,
  Visibility, Render, UI), sequentially within a stage.
- **`ResourceManager` owns assets; components hold `Handle<T>`.** Assets are
  identified by a unique `name`; serialization references them by name.

## House style (summary)

`Engine::` namespace for engine code, `Core::` for vkmGL wrappers. Engine includes
are module-qualified (`#include "ecs/scene.h"`); backend includes are flat
(`gl_`-prefixed). Class members are `m_`+camelCase; struct members are bare;
`#pragma once`; K&R braces, 4-space indent; ASCII only; no decorative comment
separators. Full guide:
[claude_helper/guides/code-style.md](claude_helper/guides/code-style.md).

## Reference

Per-subsystem deep dives are under
[claude_helper/reference/](claude_helper/reference/) (architecture, ecs, resources,
threading, editor, building, and `system/` for rendering, lighting, visibility,
hierarchy, animation, events, io). Start with
[project-overview.md](claude_helper/reference/project-overview.md).
