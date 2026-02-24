# Threading

The engine uses a singleton work-stealing thread pool for parallel workloads.

## Key Files

- `src/engine/platform/threading/thread_pool.h` -- ThreadPool + WorkerQueue

## ThreadPool

Singleton accessed via `ThreadPool::get()`. Creates `hardware_concurrency()` worker threads on first access.

### API

```cpp
auto& pool = ThreadPool::get();

// Fire-and-forget
pool.enqueue([]() { /* work */ });

// Submit with future
auto future = pool.submit([](int x) { return x * 2; }, 42);
int result = future.get();

// Parallel iteration
pool.parallelFor(0, count, grainSize,
    [&](size_t start, size_t end, size_t workerIdx) {
        for (size_t i = start; i < end; ++i) {
            // process item i
        }
    }
);

// Block until all tasks complete
pool.waitIdle();

// Query
size_t workers = pool.size();
int idx = ThreadPool::workerIndex();  // -1 if not a worker thread
```

## Work-Stealing Design

Each worker thread has a local `WorkerQueue` (deque + mutex + atomic size hint).

### Scheduling Policy

When a worker needs work:
1. **Local queue** (LIFO pop) -- best cache locality
2. **Global queue** (FIFO pop) -- externally enqueued tasks
3. **Steal from others** (FIFO from back, try_lock) -- non-blocking steal attempt

### Task Submission

- `enqueue()`: If called from a worker thread, pushes to that worker's local queue. Otherwise pushes to the global queue.
- `parallelFor()`: Pushes one task per worker directly to their local queues (round-robin), bypassing the global queue for lower contention.

### parallelFor Details

`parallelFor(begin, end, grain, function)` divides the range into chunks:

1. Creates one task per worker thread
2. Tasks use an atomic counter to grab chunks of `grain` size
3. Each task calls `function(startIdx, endIdx, workerIdx)` for each chunk
4. Uses a batch-scoped condition variable to wait only for this batch (not all in-flight tasks)

### Memory Ordering

- `m_inFlightTaskCount`: `seq_cst` for correct idle detection
- `WorkerQueue::sizeHint`: `relaxed` (approximate, used only for fast empty check)
- `parallelFor` atomic counter: `relaxed` (ordering not needed, just uniqueness)

## Usage in Engine

The primary consumer is `VisibilitySystem`, which uses `parallelFor` to cull entities across all workers with per-worker scratch buffers.
