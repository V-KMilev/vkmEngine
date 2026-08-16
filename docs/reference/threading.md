# Threading

The engine uses one singleton thread pool for data-parallel workloads. It is a
**shared work-queue pool**, not a per-worker work-stealing design: all workers pull
from a single queue of `std::function<void()>` guarded by one mutex and two
condition variables.

## Key file

- `src/engine/platform/threading/thread_pool.h` - `ThreadPool` + the free
  `parallelFor` functions

## ThreadPool

Singleton via `ThreadPool::get()`. Spawns `hardware_concurrency()` worker threads
(at least one - the count is allowed to be unknown). Non-copyable, non-movable;
the constructor and destructor are private.

```cpp
class ThreadPool {
    public:
        static ThreadPool& get();

        size_t threadCount() const;

        void addTask (std::function<void()> && task);                 // fire and forget
        void addTasks(std::vector<std::function<void()>> && tasks,
                      std::atomic<size_t>& pending);                  // one lock + one notify_all
        void waitForBatch(std::atomic<size_t>& pending);              // block on that batch only

        void shutdown();                              // join the workers early

        static bool isWorkerThread();                 // true on a pool-owned thread
    // ...
};
```

**Waiting is per batch, never global.** The pool is shared with the async asset
decodes (texture, mesh, cooked), so a barrier on "everything in flight" would park
a frame behind an unrelated file read. `addTasks` raises the caller's own `pending`
counter and each task drops it as it retires - including a task that throws, which
the worker loop catches - and `waitForBatch` sleeps until that counter, and only
that counter, reaches zero. A fire-and-forget `addTask` carries no counter.

`shutdown()` joins the workers while the rest of the program is still standing.
`Engine::run` calls it once the main loop exits: the pool is a function-local
static, so its own destructor runs after the singletons an in-flight decode pushes
into. It is idempotent, and `parallelFor` sweeps serially once the workers are gone.

Internals: a shared `std::deque` of (task, batch counter) pairs, a `m_tasksMutex`,
a `m_tasksCV` (wakes workers when tasks arrive) and a `m_doneCV` (signalled when a
batch counter hits 0). There is no local/global queue split and no stealing.

## parallelFor (free functions)

`parallelFor` is a **free function**, not a method. Two overloads:

```cpp
// Explicit grain.
template<class Function>
void parallelFor(size_t count, size_t grain, Function && function);

// Auto grain.
template<class Function>
void parallelFor(size_t count, Function && function);
```

The callback is invoked per index. It may take the index (`function(size_t i)`) or
nothing (`function()`); the implementation picks the form via
`if constexpr (is_invocable_v<Function, size_t>)`.

Behavior:

- **The calling thread participates.** It runs the first chunk inline instead of
  spinning in the wait, then waits for its own batch - so small ranges pay no
  pool-dispatch tax.
- **Auto-grain threshold.** Below `MIN_PARALLEL = 2048` items, the auto-grain
  overload sets `grain == count`, which submits zero tasks and sweeps the range
  serially on the caller - and with nothing submitted the wait returns without even
  taking the lock. At or above it, grain is `count / (threadCount + 1)` (the `+1` is
  the participating main thread).
- **Re-entrancy is serial.** Calling `parallelFor` from inside a worker (i.e.
  `isWorkerThread()` is true) falls back to a serial sweep: with every worker blocked
  on chunks queued behind the workers themselves, nothing would be left to run them.
  Do not nest parallel dispatch.

```cpp
parallelFor(entities.size(), [&](size_t i) {
    cull(entities[i]);
});
```

## Usage in the engine

`parallelFor` is the per-system scaling lever (the framework does not parallelize
systems against each other - see [architecture.md](architecture.md)). The main
consumers are `VisibilitySystem` (culling across the visible set with per-thread
scratch buffers), `HierarchySystem` (resolving world transforms by depth bucket),
and `GLShadowData::cullCasters` (one task per cascade, spot and cube face, each
writing its own caster batch).
For correctness, parallel work writes into pre-sized per-index or per-thread buffers
rather than sharing mutable state across the loop.
