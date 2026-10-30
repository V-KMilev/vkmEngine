# Threading

The engine has one thread pool, for data-parallel work inside a system and for
background jobs (asset decodes, bakes). Systems themselves run one after another,
never against each other ([architecture.md](architecture.md#stages)); a system
scales by splitting its own loop with `parallelFor`.

## parallelFor

A free function in `vkm_platform/threading/thread_pool.h`, the call most code wants (the pool is
vkmPlatform's, `Platform::parallelFor` in engine code):

```cpp
parallelFor(entities.size(), [&](size_t i) {
    cull(entities[i]);
});
```

The callback takes the index. A small range runs serially on the caller; a larger one
is cut into several chunks per thread, so a preempted thread's share goes to the
others. The caller runs a chunk too, and a throw reaches it after the whole range has
finished. Called from a worker, it runs serially on that worker.

## ThreadPool

`ThreadPool::get()` is the singleton. It spawns one worker fewer than the
physical cores the process may run on (its affinity; SMT siblings counted once),
because the calling thread runs work too and the driver, audio and OS need room
(a row in [decisions.md](decisions.md#what-has-been-decided)).

- `addTask(task)` - fire and forget, for background work. Nobody waits on it, so
  a throw is caught and logged by the worker; the task reports its own errors.
  A task captures values and touches nothing the frame owns - not the
  `ResourceManager`, the `EventBus` (main thread only) or a `Scene`'s storage.
- `addBatch(batch, count)` / `waitForBatch(batch)` - a `ThreadPool::Batch` is a task
  called per index, for work the frame waits on.
- `shutdown()` - joins the workers. `Engine::run` calls it when the main loop exits;
  after it, `parallelFor` runs serially.

## The rules

One thread runs the frame. A system that needs more cores forks inside itself with
`parallelFor` and joins before it returns. Within that:

- **A parallel body writes only its own index's output**, or per-thread scratch sized
  before the fork. Never a shared container, never a counter without an atomic.
- **A parallel body never changes the component graph** - no create, destroy, add or
  remove.
- **GL is the main thread's.** Workers decode; the frame uploads.
- **Background work returns through a queue.** An asset decode runs on the pool via
  `addTask` and hands its result to `AsyncLoadQueue`; `AsyncLoaderSystem` applies it on
  the main thread ([io.md](io.md)), and a result for an asset that was replaced in the
  meantime is recognized and dropped.
- **`thread_local` scratch lives in a `.cpp`, in an anonymous namespace** - never in an
  engine header. Instantiated in a gameplay module with a destructor, it pins the module in
  memory for the frame thread's life, so every script reload leaks the old module. No check
  holds this; review does.

## How it works inside

How the pool waits - per batch, never on everything, so a frame never parks behind a file
read - and why batches are served before tasks is vkmPlatform's
(`modules/vkmPlatform/docs/how-it-works.md`, Threads).

The main `parallelFor` users are `VisibilitySystem`, `HierarchySystem`,
`AnimationSystem`, `SkeletalAnimationSystem` and the physics' narrowphase, which vkmPhysics hands
`PhysicsSystem` as a `Physics::Workers` job ([physics.md](physics.md#the-narrowphase)).
`GLShadowPlan::cullCasters` forks
a batch by hand (one index per atlas tile, cube face and strip of a held layer) and
`GLShadowPlan::finishCull` waits on it as the shadow pass begins, so the rest of
the backend's frame build and the passes before it run beside the cull. Only a
frame that runs the shadow pass plans shadows, so none ends with the cull still
reading its objects, which the next frame rewrites.

## Key files

- `modules/vkmPlatform/src/vkm_platform/threading/thread_pool.h` - `ThreadPool`, `parallelFor`
