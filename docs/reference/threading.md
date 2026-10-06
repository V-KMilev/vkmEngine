# Threading

The engine uses one singleton thread pool for data-parallel workloads. It is a
**shared work-queue pool**, not a per-worker work-stealing design: all workers pull
from the same two queues - the frame's and the background's - guarded by one
mutex and two condition variables.

## Key file

- `src/engine/platform/threading/thread_pool.h` - `ThreadPool` + the free
  `parallelFor` functions

## ThreadPool

Singleton via `ThreadPool::get()`. Spawns one worker fewer than
`hardware_concurrency()`, because the thread that calls `parallelFor` runs a chunk
too - a worker per core would make one more thread runnable than there are cores.
At least one, since the count is allowed to be unknown. Non-copyable,
non-movable; the constructor and destructor are private.

```cpp
class ThreadPool {
    public:
        static ThreadPool& get();

        size_t threadCount() const;

        class Batch;                                                  // indexed tasks, borrowed

        void addTask (std::function<void()> && task);                 // fire and forget
        void addBatch(Batch& batch);                                  // one queue entry, any size
        void waitForBatch(Batch& batch);                              // help with it, then block on it

        void shutdown();                              // join the workers early

        static bool isWorkerThread();                 // true on a pool-owned thread
    // ...
};
```

**One batch form: borrowed.** A `Batch` is a count and a task called with each
index; the caller owns both, the pool borrows them, and the whole batch is one
queue entry that workers claim indices from one at a time - so submitting it
allocates nothing, whatever its size. `parallelFor` keeps its batch on its stack
because it blocks until the batch is done; a caller that returns before it joins
keeps its batch and task on the object the tasks write into.

**Waiting is per batch, never global.** The pool is shared with the async asset
decodes (texture, mesh, cooked) and background bakes, so a barrier on
"everything in flight" would park a frame behind an unrelated file read. A batch
raises its own `pending` counter and each index drops it as it retires, and
`waitForBatch` sleeps until that counter, and only that counter, reaches zero. A fire-and-forget `addTask` carries
no counter.

**The waiter helps first.** Before it sleeps, `waitForBatch` claims every index of
its batch no worker has taken yet, from wherever the batch's entry stands in the
queue, and runs them itself. A caller that slept instead would leave its own work
waiting for a worker to come free - a core idle for as long as the pool is busy
with something else.

**A throw belongs to the batch, not to the thread.** An index that throws still
retires, and the first exception any index of the batch threw is kept on it and
rethrown by `waitForBatch`, whichever thread ran it. An `addTask` has nobody
waiting to rethrow to, so the worker loop catches and logs what one throws; its
body is expected to report its own errors.

A retire is a lock-free decrement. Only the drop to zero takes the mutex, and
only to order the notify: a waiter holds the mutex from reading a nonzero count
until it is asleep, so a notify issued after taking it cannot land in between.
The counter is not touched after that decrement, because its waiter may already
have returned.

`shutdown()` joins the workers while the rest of the program is still standing.
`Engine::run` calls it once the main loop exits: the pool is a function-local
static, so its own destructor runs after the singletons an in-flight decode pushes
into. It is idempotent, and `parallelFor` sweeps serially once the workers are gone.

**Two queues, chosen by the shape of the call.** Batches land in
`m_frameTasks` and `addTask` in `m_backgroundTasks`, and a worker drains the
first before it looks at the second. The reason is the counter above: somebody
is blocked in `waitForBatch` until a batch empties and that somebody is the
frame, while every `addTask` caller - an asset decode or a background bake - retires
against nothing.
The order does not starve a decode for long: a worker reaches the second queue
as soon as the first holds nothing unclaimed, and only the frame fills the
first - `parallelFor` blocks its caller until its range is done, and the one
batch forked by hand (below) is joined before the pass that reads it. That batch
stays out while the frame goes on, so "one batch at a time" is not the
guarantee; what holds is that every batch in the queue belongs to the frame in
progress.

Internals: those two `std::deque`s of queued work - an owned task, or a batch
that stays in the queue until its last index is claimed, carrying the counter
its indices retire against - a `m_tasksMutex`, a `m_tasksCV` (wakes workers when tasks
arrive) and a `m_doneCV` (signalled when a batch counter hits 0). The split is by
*who is waiting*, not per worker: there is no local/global queue and no
stealing.

## parallelFor (free functions)

`parallelFor` is a **free function**, not a method. Two overloads:

```cpp
// Explicit grain.
template<class Function>
void parallelFor(size_t count, size_t grain, Function&& function);

// Auto grain.
template<class Function>
void parallelFor(size_t count, Function&& function);
```

The callback is invoked per index. It may take the index (`function(size_t i)`) or
nothing (`function()`); the implementation picks the form via
`if constexpr (is_invocable_v<Function, size_t>)`.

Behavior:

- **The calling thread participates.** It runs the first chunk inline, then any
  chunk no worker has claimed, and only then sleeps on its own batch - so small
  ranges pay no pool-dispatch tax, and a pool busy elsewhere does not stall it.
- **One batch per call.** Every chunk after the first is an index of a single
  `Batch` on `parallelFor`'s own stack, so a call queues one entry and allocates
  nothing however many chunks it has.
- **Auto-grain threshold.** Below `MIN_PARALLEL = 2048` items, the auto-grain
  overload sets `grain == count`, which submits nothing and sweeps the range
  serially on the caller - and with nothing submitted the wait returns without even
  taking the lock. At or above it, grain is `count / (threadCount + 1)` (the `+1` is
  the participating main thread).
- **A throw from any chunk reaches the caller.** The batch, its counter and the
  chunk adapter it borrows all live in `parallelFor`'s own frame, so the batch is
  joined before anything leaves it, the unwinding path included. What then
  propagates is the caller's own chunk's exception if it threw, and otherwise the
  first a queued chunk threw.
- **Re-entrancy is serial.** Calling `parallelFor` from inside a worker (i.e.
  `isWorkerThread()` is true) sweeps the range serially on that worker. The
  reason is cost, not deadlock, and `ThreadPool::isWorkerThread` states it.

```cpp
parallelFor(entities.size(), [&](size_t i) {
    cull(entities[i]);
});
```

## Usage in the engine

`parallelFor` is the per-system scaling lever (the framework does not parallelize
systems against each other - see [architecture.md](architecture.md)). The main
consumers are `VisibilitySystem` (culling every mesh into pre-sized per-index
arrays), `HierarchySystem` (resolving world transforms a level of the tree at a
time) and `PhysicsSystem`'s narrowphase (colliding the broadphase's pairs a chunk
at a time). For correctness, parallel work writes into pre-sized per-index or
per-chunk buffers rather than sharing mutable state across the loop.

The narrowphase is the one consumer that merges rather than writing in place: a
pair yields any number of manifolds, and several pairs share a body. Each chunk
writes only its own buffers, kept on the system and cleared each tick, and a
serial merge after the join reads them in chunk order, so what reaches the
solver, the bodies and the event bus is in pair order whichever thread ran
which chunk ([physics.md](physics.md#the-narrowphase)).

One consumer forks and joins by hand rather than through `parallelFor`:
`GLShadowData::cullCasters` queues one batch, an index per cascade, spot and cube
face, and returns, and `GLShadowData::finishCull` is the `waitForBatch`,
called by the backend just before the first pass. Everything the backend builds
between the two - UBOs, the drawable partition, the skin palette, the opaque
batch - reads none of the batches, so it runs beside the cull instead of after
it. It is the same fork-join, with the join placed where the result is first
needed; the batch and its task live on the object the tasks write into, and `build` and
the destructor both join first so no task ever outlives what it writes.
