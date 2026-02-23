# Architecture Review & Improvement Roadmap

Analysis of the engine state after the clean-history refactoring. Organized by priority and system, identifying what's missing, what's suboptimal, and what needs redesign for a production-quality engine.

---

## Critical Missing Features

These are features whose absence fundamentally limits the engine's usefulness.

### Shadow Mapping
PBR lighting without shadows is visually incorrect. `LightData` already has a `castShadows` field but nothing in the pipeline generates or consumes shadow maps. Needed: depth-only render pass per shadow-casting light, shadow atlas or per-light FBOs, PCF or VSM filtering in the PBR fragment shader.

### Post-Processing Pipeline
The forward pass renders directly to the default framebuffer with no post-process chain. Missing: HDR render target, tone-mapping (ACES/Reinhard), bloom, FXAA/TAA anti-aliasing. The `RenderTarget` abstraction is already in place — this is the natural next step.

### Scene Serialization
The editor can create/modify entities but cannot save or load scenes. All state is lost on exit. Need a scene format (JSON or binary) with entity/component serialization and resource reference resolution.

### Async Resource Loading
All resource creation via `ResourceManager::add()` is synchronous on the main thread. Loading a large texture or mesh blocks the frame loop. Need: loading queue, placeholder resources, background thread upload, PBO-based async texture upload.

### Skeletal Animation
Only property-track animation (position/rotation/scale) exists. No bone hierarchies, skinned meshes, blend shapes, or animation blending/layering. This limits the engine to rigid-body animation.

---

## Performance Issues (Hot Paths)

### No Spatial Partitioning
`VisibilitySystem` iterates ALL mesh entities linearly every frame (`parallelFor(0, meshCount, ...)`). For 10K+ entities, every one is tested against the frustum. A BVH, octree, or uniform grid would reduce culling to O(log N) or O(visible). This is the single biggest performance bottleneck for large scenes.

### Occlusion Culling is a Placeholder
`occlusion_culler.h` returns `true` unconditionally. The engine renders every frustum-visible object, including fully occluded ones. Hi-Z occlusion or software rasterization would eliminate significant overdraw.

### No LOD System
The screen-size culler rejects entities below a pixel threshold, but there's no mechanism to switch mesh LODs based on screen coverage. Objects pop in/out abruptly rather than gracefully degrading quality.

### `unordered_map` in Hot Paths
Several performance-critical paths use `std::unordered_map`:
- `VisibilitySystem`: world matrix cache (`unordered_map<uint32_t, glm::mat4>`) — cleared and rebuilt every frame, causing heap allocation churn
- `GLView`: GPU resource maps (`unordered_map<uint32_t, unique_ptr<GLMesh>>`) — hash lookups per resource
- `EventSystem`: listener map (`unordered_map<string, ...>`) — string hashing on every emit

Replace with flat arrays indexed by handle ID, or flat sorted vectors.

### ECS Multi-Component Query Performance
`Scene::forEach<A, B, ...>` iterates the primary component densely but performs random-access `SparseSet::contains()` on every secondary set per entity. This causes scattered reads across multiple sparse arrays, thrashing L1 cache. Production engines use archetype tables or grouped iteration for contiguous multi-component access.

### RenderView Rebuilt Every Frame
`RenderView::build()` copies all visibility entries into `DrawableData` structs (88 bytes each), sorts them, and gathers all lights every frame — even when nothing changed. No dirty-checking or incremental update.

### ThreadPool Allocation Overhead
`std::function<void()>` for task storage can heap-allocate when captures exceed the small-buffer size (~16-32 bytes). `std::deque` per-worker queue has pointer-chasing overhead. A fixed-size ring buffer with type-erased small tasks would eliminate allocations in the scheduling hot path.

---

## Architecture Issues

### Singleton Pattern
`Engine::get()` is a god-object singleton owning Scene, ResourceManager, WindowManager, StatisticTracker, and the system pipeline. Any code can reach global mutable state. Problems:
- Unit testing individual systems is impossible without the full Engine
- No dependency injection
- Hidden dependencies (systems access Engine::get() internally rather than declaring what they need)

**Recommendation:** Move toward explicit dependency injection. Systems receive their dependencies through constructors or `FrameContext`, not through global access.

### System Pipeline is Sequential-Only
Systems execute in a flat `vector<unique_ptr<System>>` with no dependency graph. Independent systems (e.g., AnimationSystem and EventSystem) cannot run concurrently. The `System` base class has only `update()` — no mechanism to declare read/write component dependencies for automatic parallelism.

### Minimal System Lifecycle
`System` has only `virtual void update(FrameContext&)`. Missing lifecycle hooks:
- `init()` / `shutdown()` — systems currently do initialization in constructors, coupling construction to runtime state
- `fixedUpdate()` — needed for physics/deterministic simulation
- `onSceneChange()` — no notification when scene structure changes
- No system enable/disable or reordering at runtime

### ResourceManager Uses Closed Type Set
Only `MeshAsset`, `TextureAsset`, `MaterialAsset` are supported via `if constexpr` dispatch in `getStorage<T>()`. Adding a new type (ShaderAsset, AudioAsset) requires editing ResourceManager. Should use a type-erased registry like Scene does for components.

### Single Global Version Counter
`ResourceManager::m_globalVersion` is bumped by any `commit()` on any resource type. This causes false-positive "something changed" signals in consumers that only care about specific types. Per-type version tracking would be more precise.

### No Resource Reference Counting
`Storage<T>` provides add/remove but no reference count. If an entity holds a `MeshHandle` and the resource is removed via `ResourceManager::remove()`, the handle becomes stale. The generational check prevents UB, but there's no protection against accidental removal of in-use resources.

### No Resource Dependencies
Materials reference textures via handles, but there's no dependency graph. Removing a texture doesn't notify or invalidate materials that use it.

---

## Rendering Gaps

### Forward-Only Rendering
Single forward pass with no deferred rendering option. Cannot efficiently handle many lights (each light requires a pass through all geometry, or a single pass with a MAX_LIGHTS cap of 32).

### No Shader Permutation System
Only 3 shader slots (Opaque/Transparent/Unlit) with hard-coded paths. No mechanism for feature flags (normal mapping on/off, skinning, etc.). All features must be in a single uber-shader.

### Naive Transparency
Simple alpha blending with depth writes disabled. No order-independent transparency (OIT), no depth peeling. Overlapping transparent objects produce incorrect results depending on draw order.

### No Front-to-Back / Back-to-Front Sorting
Drawables are sorted by (materialType, material, mesh) for batching but not by depth. Opaque geometry should be front-to-back for early-Z, transparent should be back-to-front for correct blending.

### Per-Batch VAO Modification
`instanceBuffer->attachToVAO` modifies VAO state per draw call. If batches share the same mesh, the VAO binding is redundant. Persistent mapped buffers or multi-draw-indirect would be more efficient.

### No GPU Culling
All culling is CPU-side. Compute-shader frustum culling with indirect draw commands would handle 100K+ objects with minimal CPU overhead.

---

## ECS Specific Issues

### No Archetype Storage
The open registry uses one SparseSet per type. Multi-component iteration does cross-set random access. Archetype tables (like EnTT groups or flecs archetypes) guarantee contiguous access for common component combinations.

### `destroyEntity` Scans All Component Types
Loops over every registered component type to check if the entity had that component: O(C) per destruction. A per-entity component bitmask would make this O(K) where K = components the entity actually has.

### `typeId<T>()` is Not Thread-Safe
`nextTypeId()` uses a non-atomic static counter. If called from multiple threads simultaneously for new types, this is a data race. Should use `std::atomic<uint32_t>`.

### Unbounded Sparse Array Growth
`SparseSet::ensureCapacity()` resizes to `key + 1`. After many create/destroy cycles with sparse indices, the sparse array can grow far larger than the dense array. No compaction or paging.

---

## Event System Issues

### String-Keyed Dispatch
`std::unordered_map<std::string, ...>` for listener lookup. String hashing on every `emit()` is expensive. Should use integer keys (hashed at registration time) or compile-time type dispatch.

### No Typed Events
All events use `std::function<void()>` callbacks with string names. No type-safe event data payload. Passing data requires lambda captures, which is error-prone and prevents introspection.

### Potential Deadlock Pattern
`emit()` copies callbacks under lock then executes outside lock (correctly). But if a callback calls `push()` with IMMEDIATE priority, that `push()` will call `emit()` recursively, which re-acquires `m_listenerMutex`. The mutexes are not recursive.

---

## Editor Gaps

### No Undo/Redo
All operations are immediate and irreversible. Need a command pattern with undo stack.

### No 3D Transform Gizmos
Inspector has drag controls for position/rotation/scale, but no visual manipulators in the viewport (translate/rotate/scale handles).

### No Multi-Selection
`m_selectedEntity` is a single EntityId. Cannot select and operate on multiple entities.

### No Asset Import
Cannot import external models (glTF, OBJ, FBX) or textures from disk. Only procedural primitives.

### No Drag-and-Drop Reparenting
Hierarchy reparenting requires the inspector panel, not drag-and-drop in the hierarchy tree.

### Hard-Coded Panel Sizes
Panel widths are fixed constants, not resizable by the user.

### Direct GL Calls
`editor_system.cpp` calls `glPolygonMode` directly, bypassing the render backend abstraction. Breaks non-GL backends.

### `duplicateEntity` Manually Lists Component Types
Every component type must be explicitly handled. New component types are silently dropped during duplication.

---

## Hierarchy Issues

### Fixed Depth Limit
`computeWorldMatrix` uses a stack of 32. Hierarchies deeper than 32 are silently truncated. `setParent` doesn't enforce this limit, allowing construction of invalid hierarchies.

### No Topological Sort for World Matrix Computation
Parent chains are walked per-entity. A pre-pass computing world matrices in topological order (parent-before-child) would be O(1) per entity after the sort.

### Recursive `destroyHierarchy`
Can cause stack overflow for very wide/deep hierarchies. Should use iterative traversal with explicit stack.

### Iterator Invalidation Risk
`removeFromParent` conditionally removes the Hierarchy component via `scene.remove<Hierarchy>()`. If called during `forEach<Hierarchy>` iteration, this invalidates the iterator.

---

## Recommended Priority Order

### Phase 1: Rendering Foundations
1. Shadow mapping (depth pass + PCF)
2. Post-processing pipeline (HDR target, tone-mapping, bloom)
3. Depth sorting (front-to-back opaque, back-to-front transparent)

### Phase 2: Performance
4. Spatial partitioning (BVH for static, rebuild for dynamic)
5. Replace `unordered_map` hot paths with flat arrays
6. GPU culling via compute shader + indirect draw
7. Make `typeId<T>()` thread-safe (atomic counter)

### Phase 3: Architecture
8. Scene serialization (JSON format)
9. Async resource loading pipeline
10. Resource reference counting
11. Type-erased ResourceManager (like Scene's component registry)

### Phase 4: ECS & Systems
12. Archetype storage or EnTT-style groups for common queries
13. System dependency graph for parallel execution
14. Per-entity component bitmask for O(K) destroy

### Phase 5: Editor
15. Undo/redo command stack
16. 3D transform gizmos (ImGuizmo integration)
17. Scene save/load
18. glTF import

### Phase 6: Animation & Advanced
19. Skeletal animation + skinning
20. LOD system
21. Occlusion culling (Hi-Z or software raster)
22. Deferred rendering path
