# Threading

The engine uses one singleton thread pool for data-parallel workloads. It is a
**shared work-queue pool**, not a per-worker work-stealing design: all workers pull
from a single `std::deque<Task>` guarded by one mutex and two condition variables.

## Key file

- `src/engine/platform/threading/thread_pool.h` - `ThreadPool` + the free
  `parallelFor` functions
- `src/engine/platform/threading/task.h` - `Task` (the work unit)

## ThreadPool

Singleton via `ThreadPool::get()`. Spawns `hardware_concurrency()` worker threads.
Non-copyable, non-movable; the constructor and destructor are private.

```cpp
class ThreadPool {
    public:
        static ThreadPool& get();

        size_t threadCount() const;

        void addTask(Task && task);
        void addTasks(std::vector<Task>&& tasks);   // one lock + one notify_all
        void waitToFinish();                          // block until the queue drains

        static bool isWorkerThread();                 // true on a pool-owned thread
    // ...
};
```

Internals: a shared `std::deque<Task>`, a `m_tasksMutex`, a `m_tasksCV` (wakes
workers when tasks arrive), a `m_doneCV` (signalled when the in-flight count hits 0),
and an atomic `m_taskCount`. There is no local/global queue split and no stealing.

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
  spinning in `waitToFinish()`, then waits for the workers - so small ranges pay no
  pool-dispatch tax.
- **Auto-grain threshold.** Below `MIN_PARALLEL = 2048` items, the auto-grain
  overload sets `grain == count`, which submits zero tasks and sweeps the range
  serially on the caller. At or above it, grain is `count / (threadCount + 1)` (the
  `+1` is the participating main thread).
- **Re-entrancy is serial.** Calling `parallelFor` from inside a worker (i.e.
  `isWorkerThread()` is true) falls back to a serial sweep, because that worker's own
  slot keeps `m_taskCount` above zero and `waitToFinish()` would otherwise deadlock.
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
scratch buffers) and `HierarchySystem` (resolving world transforms by depth bucket).
For correctness, parallel work writes into pre-sized per-index or per-thread buffers
rather than sharing mutable state across the loop.
