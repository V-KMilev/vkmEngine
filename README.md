# vkmEngine Documentation

A C++17 3D rendering engine with OpenGL backend, ECS architecture, PBR rendering, visibility culling, animation, and event systems.

## Quick Start

```bash
git submodule update --init --recursive
cmake -B build -G Ninja
cmake --build build
./build/engine
```

## Documentation Index

### Core

- [Architecture](architecture.md) -- High-level design, directory layout, execution order, conventions
- [Building](building.md) -- Prerequisites, build commands, CMake targets, external modules
- [ECS](ecs.md) -- Entity-Component-System: Scene, entities, components, queries
- [Resources](resources.md) -- Resource management, asset types, handles, versioning

### Systems

- [Rendering](system/rendering.md) -- Render pipeline, backend abstraction, passes, shader interface
- [Visibility](system/visibility.md) -- Culling pipeline, frustum/distance/screen-size culling
- [Animation](system/animation.md) -- Animation tracks, keyframes, easing, two-phase update
- [Events](system/events.md) -- Priority queue, pub/sub, thread safety

### Platform

- [Threading](threading.md) -- Thread pool, work-stealing, parallel iteration
- [Editor](editor.md) -- Camera controller, editor UI, transform gizmo

### Other

- [Style Guide](../STYLE_GUIDE.md) -- Coding conventions and formatting rules
- [Review & Roadmap](../REVIEW.md) -- Architecture review and improvement priorities
