# Event Bus

Typed pub/sub dispatcher for in-engine events. Subscribers register a
typed callback for events of type `EventT`; publishers call `emit()` to
fire synchronously or `enqueue()` to defer until the next `flush()`.

The bus is **engine infrastructure, not a System**: the Engine owns one by
value (like the Clock and WindowManager), every `FrameContext` carries it as
`ctx.events`, and the frame loop calls `flush()` at the top of the Simulation
stage - the fixed, visible point where queued events deliver. Nothing is wired
to it in `setupEngineApp`; systems just read it off the context.

## Key Files

- `src/engine/core/event/event_bus.h` -- `EventBus` (the facade)
- `src/engine/core/event/event_bus.cpp` -- `flush()`
- `src/engine/core/event/bus.h` -- per-type `Bus<EventT>` internals

## Event Types

Any user-defined struct is an event. No base class, no registration:

```cpp
struct DamageEvent { EntityId target; int amount; };
struct LevelLoaded { std::string sceneName; };
```

The type itself is the channel selector - subscribing to `DamageEvent`
sees only `DamageEvent` instances.

## Subscribing

```cpp
// Inside a system: ctx.events. From the app layer: engine.getEvents().
auto id = ctx.events.subscribe<DamageEvent>([](const DamageEvent& e) {
    applyDamage(e.target, e.amount);
});
```

`subscribe` returns a `ListenerId` for later removal. The callback runs
on the frame thread (main thread today).

## Unsubscribing

```cpp
events.unsubscribe<DamageEvent>(id);
```

Returns `true` if the listener existed and was removed. **Cannot be
called from inside a listener callback during emit/flush** - the system
asserts. Listeners that need self-unsubscribe should `enqueue` a removal
event for the next frame.

## Publishing

Two flavours:

```cpp
events.emit(DamageEvent{target, 50});      // synchronous: every listener fires now
events.enqueue(DamageEvent{target, 25});   // deferred: fires on next flush()
```

Use `emit` for tightly coupled local state changes (gameplay reaction in
the same tick). Use `enqueue` for decoupled cross-system flow (UI
reactions, asset events, latency-tolerant work).

## Flushing

`EventBus::flush()`, called by `Engine::run` at the top of the Simulation
stage, iterates every bus and drains its queue. Each enqueued event is delivered to every
listener registered for its type.

A listener that enqueues a new event during flush will see it land on
the *next* frame's flush - the bus swaps the queue at the start of
flush so re-entrant enqueues land in fresh storage.

## Threading

Main thread only. `emit`, `enqueue`, `subscribe`, `unsubscribe`, and
`flush` must all happen on the same thread (typically the engine's
update thread). If a future subsystem (e.g. physics on a worker) needs
to push events, add a mutex to `Bus<EventT>` at that point.

## Caveats

- Don't subscribe or unsubscribe from inside a listener callback during
  `emit`/`flush`. Subscribing is *technically* safe (new listeners join
  the next frame's flush) but unsubscribing trips an assert.
- A listener that enqueues an event whose bus has already been flushed
  this frame will see that event fire on the *next* frame's flush.
- Listeners are iterated by index against a frozen bound at flush
  start; a listener that takes a long time to run will block subsequent
  listeners for the same event.

## Implementation Notes

Internally each event type gets a lazily-created `Bus<EventT>` (stored
in `m_buses` keyed by `typeId<EventT>()`). The bus holds:

- a `std::vector<{ListenerId, std::function}>` of subscribers,
- a `std::vector<EventT>` queue for deferred events,
- a `flushDepth` counter that gates the unsubscribe assert.

`emit` and `flush` walk the listener vector by index, holding the size
constant so subscribes during dispatch don't grow the iteration. The
queue is swapped (not copied) at flush start.
