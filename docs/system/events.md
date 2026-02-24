# Event System

The EventSystem provides two event dispatch mechanisms: a priority queue for deferred events and a named pub/sub system for signal-based communication.

## Key Files

- `src/engine/system/event/event_system.h` -- EventSystem
- `src/engine/system/event/event.h` -- Event class + EventPriority
- `src/engine/system/event/event_listener.h` -- EventListener wrapper

## EventSystem

Inherits `System`, executes queued events each frame via `update()`.

### Priority Queue

Push events with callbacks and priorities. Events execute in priority order during the frame update.

```cpp
EventSystem& events = ...;

events.push(Event{
    []() { /* callback */ },
    EventPriority::HIGH,
    "my_event"
});
```

**IMMEDIATE** events execute inline in `push()` -- they bypass the queue entirely.

### Pub/Sub (Named Listeners)

Subscribe callbacks to named event channels. When emitted, all subscribers are invoked in priority order.

```cpp
// Subscribe (returns listener ID for later unsubscription)
uint32_t id = events.subscribe(Event{
    []() { LOG_INFO("Player spawned"); },
    EventPriority::MEDIUM,
    "player_spawn"
});

// Emit to all subscribers
events.emit("player_spawn");

// Unsubscribe
events.unsubscribe(id);

// Unsubscribe all listeners for an event
events.unsubscribeAll("player_spawn");
```

## Event

```cpp
class Event {
    EventCallback m_callback;   // std::function<void()>
    EventPriority m_priority;
    std::string m_name;
};
```

Events are move-only (non-copyable). Comparison by priority enables priority queue ordering.

## EventPriority

```cpp
enum class EventPriority : int {
    LOW       = 1,
    MEDIUM    = 2,
    HIGH      = 3,
    IMMEDIATE = 4   // executes immediately in push(), bypasses queue
};
```

## EventListener

Wraps an `Event` with a unique `uint32_t` listener ID for efficient unsubscription.

## Thread Safety

- **Queue operations**: Protected by `m_eventMutex`. Queue is swapped under lock, then drained without holding the lock (swap-under-lock pattern).
- **Listener operations**: Protected by `m_listenerMutex`. `emit()` copies callbacks under lock, then executes them after releasing the lock, so callbacks can safely subscribe/unsubscribe.
- **ID lookup**: `unordered_map<uint32_t, string>` provides O(1) listener ID to event name mapping for fast unsubscription.

## Per-Frame Execution

`EventSystem::update()` calls `executeAsync()`, which:
1. Swaps the event queue under lock (O(1))
2. Drains all events in priority order on the calling thread
