# Threading

The engine has one thread pool, for data-parallel work inside a system and for
background jobs (asset decodes, bakes). Systems run one after another, never
against each other ([architecture.md](architecture.md)); a system scales by
splitting its own loop with `parallelFor`.

## parallelFor

A free function in `platform/threading/thread_pool.h`, the call most code wants:

```cpp
parallelFor(entities.size(), [&](size_t i) {
    cull(entities[i]);
});
```

The callback takes the index or nothing. An overload
`parallelFor(count, grain, function)` sets the chunk size; without it, a range
below `MIN_PARALLEL` items runs serially on the caller, and a larger one is cut
into several chunks per thread so a preempted thread's share goes to the others.

- **The caller works too.** It runs a chunk inline, then any chunk no worker has
  claimed, and only then sleeps on its own batch.
- **A throw reaches the caller**, after the whole batch has finished.
- **Called from a worker, it runs serially** on that worker (`isWorkerThread()`).
- **No shared mutable state across indices.** Write into pre-sized per-index or
  per-chunk buffers.

## ThreadPool

`ThreadPool::get()` is the singleton. It spawns one worker fewer than the
physical cores (SMT siblings counted once), because the calling thread runs work
too and the driver, audio and OS need room (a row in
[engine.md](../guides/engine.md#4-what-has-already-been-decided)).

- `addTask(task)` - fire and forget, for background work. Nobody waits on it, so
  a throw is caught and logged by the worker; the task reports its own errors.
  A task captures values and touches nothing the frame owns - not the
  `ResourceManager`, the `EventBus` (main thread only) or a `Scene`'s storage. A
  decode posts its result to `AsyncLoadQueue`, and `AsyncLoaderSystem` lands it
  on the main thread ([io.md](io.md)).
- `addBatch(batch)` / `waitForBatch(batch)` - a `ThreadPool::Batch` is a count
  and a task called per index. The caller owns both and they must outlive the
  wait; the pool borrows them, so submitting allocates nothing. The waiter first
  runs every index no worker has taken, then sleeps; the first exception any
  index threw is rethrown by `waitForBatch`.
- `shutdown()` - joins the workers. `Engine::run` calls it when the main loop
  exits, because the pool is a function-local static that would otherwise die
  after singletons an in-flight decode still writes to. Afterwards `parallelFor`
  runs serially.

**Waiting is per batch, never global.** The pool is shared with background
decodes, and a barrier on everything in flight would park a frame behind a file
read.

**Batches go first.** Batches and tasks sit in two queues, and a worker drains
the batch queue before the task queue: a batch has the frame blocked on it, a
task does not.

## In the engine

The main `parallelFor` users are `VisibilitySystem`, `HierarchySystem`,
`AnimationSystem`, `SkeletalAnimationSystem` and `PhysicsSystem`'s narrowphase
([physics.md](physics.md#the-narrowphase)). `GLShadowData::cullCasters` forks
a batch by hand (one index per cascade, spot and cube face) and
`GLShadowData::finishCull` waits on it just before the first pass, so the rest
of the backend's frame build runs beside the cull.

## Key files

- `src/engine/platform/threading/thread_pool.h` - `ThreadPool`, `parallelFor`
