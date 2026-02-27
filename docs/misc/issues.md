# Known Issues & Improvement Roadmap

Tracked issues organized by category and priority. Sourced from [REVIEW.md](../../REVIEW.md).

---

## Critical Missing Features

### [FEAT] Shadow Mapping
PBR lighting without shadows is visually incorrect. `LightData.castShadows` exists but nothing generates or consumes shadow maps.
- Depth-only render pass per shadow-casting light
- Shadow atlas or per-light FBOs
- PCF or VSM filtering in PBR fragment shader

### [FEAT] Post-Processing Pipeline
Forward pass renders directly to default framebuffer with no post-process chain.
- HDR render target (RenderTarget abstraction already exists)
- Tone-mapping (ACES/Reinhard)
- Bloom
- FXAA/TAA anti-aliasing

### [FEAT] Scene Serialization
Editor can create/modify entities but cannot save or load. All state lost on exit.
- Scene format (JSON or binary)
- Entity/component serialization
- Resource reference resolution

### [FEAT] Async Resource Loading
All `ResourceManager::add()` calls are synchronous on main thread. Blocks frame loop.
- Loading queue with placeholder resources
- Background thread upload
- PBO-based async texture upload

### [FEAT] Skeletal Animation
Only property-track animation (position/rotation/scale). No bone hierarchies, skinned meshes, blend shapes, or animation blending.

---

## Performance

### [PERF] No Spatial Partitioning
`VisibilitySystem` iterates ALL mesh entities linearly every frame. For 10K+ entities this is the single biggest bottleneck.
- BVH, octree, or uniform grid would reduce culling to O(log N)

### [PERF] Occlusion Culling Placeholder
`occlusion_culler.h` returns `true` unconditionally. All frustum-visible objects are rendered including fully occluded ones.
- Hi-Z occlusion or software rasterization

### [PERF] No LOD System
Screen-size culler rejects below threshold but no mesh LOD switching. Objects pop in/out abruptly.

### [PERF] `unordered_map` in Hot Paths
Heap allocation churn and hash overhead in critical paths:
- `VisibilitySystem::m_worldMatrixCache` -- cleared/rebuilt every frame
- `GLView` GPU resource maps -- hash lookup per resource
- `EventSystem` listener map -- string hashing on every emit

**Fix:** Replace with flat arrays indexed by handle ID, or flat sorted vectors.

### [PERF] ECS Multi-Component Query Cache Thrashing
`Scene::forEach<A, B>` iterates first component densely but random-accesses secondary `SparseSet::contains()` per entity. Scattered reads thrash L1 cache.
- Archetype tables or grouped iteration for contiguous access

### [PERF] RenderView Rebuilt Every Frame
`RenderView::build()` copies all visibility entries (88 bytes each), sorts, and gathers lights every frame even when nothing changed.
- Dirty-checking or incremental update

### [PERF] ThreadPool Allocation Overhead
`std::function<void()>` heap-allocates for large captures. `std::deque` per-worker has pointer-chasing overhead.
- Fixed-size ring buffer with type-erased small tasks

---

## Architecture

### ~~[ARCH] Singleton God Object~~ ✓
~~`Engine::get()` owns Scene, ResourceManager, WindowManager, StatisticTracker, and system pipeline. Any code can reach global mutable state.~~
- ~~Cannot unit test individual systems~~
- ~~No dependency injection~~
- ~~Hidden dependencies~~

**Done:** FrameContext enriched with WindowManager& and StatisticTracker&. All editor Engine::get() call sites replaced with ctx access.

### ~~[ARCH] Sequential-Only System Pipeline~~ ✓
~~Systems execute in flat vector with no dependency graph. Independent systems cannot run concurrently. No mechanism to declare read/write component dependencies.~~

**Done:** SystemAccess declarations (reads/writes TypeId vectors) + write-write conflict validation at init.

### ~~[ARCH] Minimal System Lifecycle~~ ✓
~~`System` only has `update()`. Missing init/shutdown/enable-disable.~~

**Done:** Added `init(FrameContext&)`, `shutdown()`, `isEnabled()`/`setEnabled()`. Engine calls init on first frame, shutdown in reverse order on exit.

### ~~[ARCH] Closed ResourceManager Type Set~~ ✓
~~Only MeshAsset, TextureAsset, MaterialAsset via `if constexpr`. Adding new types requires editing ResourceManager.~~

**Done:** Rewritten to open type-erased registry using `vector<unique_ptr<IStorage>>` indexed by `typeId<T>()`.

### ~~[ARCH] Single Global Version Counter~~ ✓
~~`ResourceManager::m_globalVersion` bumped by any `commit()` on any type. False-positive "changed" signals for type-specific consumers.~~

**Done:** Per-type version tracking via `IStorage::typeVersion()`. GLView sync uses `getTypeVersion<T>()` per resource type.

### ~~[ARCH] No Resource Reference Counting~~ ✓
~~`Storage<T>` has no ref count. Removing a resource while entities hold handles makes them stale (generational check prevents UB but not accidental removal).~~

**Done:** `Storage<T>` has parallel `m_refCounts` sparse array with `acquire()`/`release()`/`refCount()`. `remove()` asserts on non-zero refCount.

### ~~[ARCH] No Resource Dependencies~~ ✓
~~Materials reference textures via handles but no dependency graph. Removing a texture doesn't invalidate dependent materials.~~

**Done:** ResourceManager dependency graph via `addDependency()`/`removeDependency()`/`hasDependents()`. `remove()` warns about active dependents.

---

## Rendering

### [RENDER] Forward-Only
Single forward pass, no deferred option. Cannot efficiently handle many lights (MAX_LIGHTS = 32 cap).

### [RENDER] No Shader Permutations
Only 3 material type slots (Opaque/Transparent/Unlit). No feature flags (normal mapping on/off, skinning, etc.). All features in single uber-shader.

### [RENDER] Naive Transparency
Simple alpha blending, no OIT, no depth peeling. Overlapping transparent objects render incorrectly depending on draw order.

### [RENDER] No Depth Sorting
Drawables sorted by (materialType, material, mesh) for batching but not by depth. Opaque should be front-to-back (early-Z), transparent back-to-front.

### [RENDER] Per-Batch VAO Modification
`instanceBuffer->attachToVAO` modifies VAO state per draw call. Redundant when batches share mesh.
- Persistent mapped buffers or multi-draw-indirect

---

## ECS

### [ECS] No Archetype Storage
One SparseSet per type. Multi-component iteration does cross-set random access.
- Archetype tables (EnTT groups / flecs archetypes) for contiguous access

### ~~[ECS] `destroyEntity` Scans All Component Types~~ ✓
~~Loops over every registered component type per destruction: O(C).~~

**Done:** Per-entity `uint64_t` component bitmask in Scene. `destroyEntity()` iterates only set bits via `__builtin_ctzll`, giving O(K) where K = actual component count. Bitmask updated automatically in `add<T>()` and `remove<T>()`.

### ~~[ECS] `typeId<T>()` Not Thread-Safe~~ ✓
~~`nextTypeId()` uses non-atomic static counter. Data race if called from multiple threads for new types.~~

**Done:** Replaced with `std::atomic<TypeId>` using `fetch_add(1, memory_order_relaxed)`.

### ~~[ECS] Unbounded Sparse Array Growth~~ ✓
~~`SparseSet::ensureCapacity()` resizes to `key + 1`. After many create/destroy cycles, sparse array can grow much larger than dense. No compaction or paging.~~

**Done:** Added `compact()` to `ISparseSet` and `SparseSet<T>` — scans for max live key and shrinks sparse array. `Scene::compact()` convenience method compacts all component storages. Also added `sparseCapacity()` for diagnostics.

---

## Events

### [EVENT] String-Keyed Dispatch
`unordered_map<string, ...>` for listener lookup. String hashing on every `emit()`.
- Use integer keys (hash at registration) or compile-time type dispatch

### [EVENT] No Typed Events
All events use `function<void()>` with string names. No type-safe data payload.
- Lambda captures are error-prone and prevent introspection

### [EVENT] Potential Deadlock
`emit()` correctly copies callbacks under lock. But if callback calls `push()` with IMMEDIATE priority, it calls `execute()` which recurses into `m_listenerMutex` (non-recursive).

---

## Hierarchy

### [HIER] Fixed Depth Limit
`computeWorldMatrix` stack of 32. Deeper hierarchies silently truncated. `setParent` doesn't enforce.

### [HIER] No Topological Sort
Parent chains walked per-entity. A pre-pass computing world matrices parent-before-child would be O(1) per entity.

### [HIER] Recursive `destroyHierarchy`
Stack overflow risk for deep/wide hierarchies. Should use iterative traversal.

### [HIER] Iterator Invalidation
`removeFromParent` removes Hierarchy component via `scene.remove<Hierarchy>()`. Unsafe if called during `forEach<Hierarchy>`.

---

## Editor

### [EDITOR] No Undo/Redo
All operations immediate and irreversible. Need command pattern with undo stack.

### [EDITOR] No Multi-Selection
`m_selectedEntity` is single EntityId. Cannot operate on multiple entities.

### [EDITOR] No Asset Import
Cannot import glTF, OBJ, FBX, or textures from disk. Only procedural primitives.

### [EDITOR] No Drag-and-Drop Reparenting
Hierarchy reparenting only via inspector panel.

### [EDITOR] Hard-Coded Panel Sizes
Panel widths are fixed constants, not user-resizable.

### [EDITOR] Direct GL Calls
`editor_system.cpp` calls `glPolygonMode` directly, bypassing render backend abstraction.

### [EDITOR] `duplicateEntity` Manual Component List
Every component type manually listed. New types silently dropped during duplication.

---

## Priority Roadmap

### Phase 1: Rendering Foundations
1. Shadow mapping (depth pass + PCF)
2. Post-processing pipeline (HDR, tone-mapping, bloom)
3. Depth sorting (front-to-back opaque, back-to-front transparent)

### Phase 2: Performance
4. Spatial partitioning (BVH)
5. Replace `unordered_map` hot paths with flat arrays
6. GPU culling (compute shader + indirect draw)
7. Thread-safe `typeId<T>()` (atomic counter)

### Phase 3: Architecture
8. Scene serialization (JSON)
9. Async resource loading
10. Resource reference counting
11. Type-erased ResourceManager

### Phase 4: ECS & Systems
12. Archetype storage / grouped iteration
13. System dependency graph for parallel execution
14. Per-entity component bitmask

### Phase 5: Editor
15. Undo/redo command stack
16. Scene save/load
17. glTF import

### Phase 6: Advanced
18. Skeletal animation + skinning
19. LOD system
20. Occlusion culling (Hi-Z)
21. Deferred rendering path
