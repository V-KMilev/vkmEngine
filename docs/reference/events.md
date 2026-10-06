# Event Bus

Typed pub/sub dispatcher for in-engine events. Subscribers register a
typed callback for events of type `EventT`; publishers call `emit()` to
fire synchronously or `enqueue()` to defer until the next `flush()`.

The bus is **engine infrastructure, not a System**: the Engine owns one by
value (like the Clock and WindowManager), every `FrameContext` carries it as
`ctx.events`, and `flush()` is called at the top of the Simulation stage - the
fixed, visible point where queued events deliver. That point is reached **once
per tick**, inside the fixed loop, and once more per frame for whatever was
queued outside a tick or by the last tick of the frame; `flush` drains, so the
second never repeats the first. Delivering on the frame instead would put a
reaction however many ticks later the frame rate decided. Nothing is wired to it
in `setupEngineApp`; systems just read it off the context.

## Event Types

Any user-defined struct is an event. No base class, no registration:

```cpp
struct DamageEvent { EntityId target; int amount; };
struct LevelLoaded { std::string sceneName; };
```

The type itself is the channel selector - subscribing to `DamageEvent`
sees only `DamageEvent` instances.

An event type declared in gameplay code is the module's, and so is the bus the
first `subscribe` to it creates - its vtable, its destructor and the events it
has queued. The bus lives here, which outlives the module, so a script reload
drops every bus nothing is listening on any more before the old library is
unmapped (`EventBus::dropIdleBuses`, called by `ScriptModule`). A behavior's
`subscribe()` is what makes that exact: it is tracked and dropped when the
session ends, so by then the module's buses are the empty ones and the engine's
still hold their systems' listeners. This is the reason a behavior's
`events()` is an `EventSender` - emit and enqueue, no subscribe - so the tracked
`subscribe()` is the only listener it can register; see
[Scripting](scripting.md).

## Subscribing

```cpp
// Inside a system: ctx.events. From the app layer: engine.getEvents().
auto id = ctx.events.subscribe<DamageEvent>([](const DamageEvent& e) {
    applyDamage(e.target, e.amount);
});
```

`subscribe` returns a `ListenerId` for later removal. The callback runs
on the frame thread, which is the main thread.

## Unsubscribing

```cpp
events.unsubscribe<DamageEvent>(id);
```

Returns `true` if the listener existed and was removed. **Callable from inside a
listener callback, including on itself**: `emit` and `flush` walk by index, so
mid-dispatch the entry is marked dead rather than erased, and reaped once the
outermost dispatch unwinds. It is not emptied either: a listener unsubscribing
itself is running inside the callable it holds, and clearing that would free
the captures the rest of its callback runs on. A listener that subscribed during this same
dispatch is waiting in the pending list and comes straight back out of it, so a
one-shot can subscribe and cancel itself in one callback.

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
the *next* flush - the next tick's, or the frame's own when no tick is left -
because every bus swaps its queue aside before any of them delivers, so
re-entrant enqueues land in fresh storage whether they name the type being
delivered or another one.

## Threading

Main thread only. `emit`, `enqueue`, `subscribe`, `unsubscribe`, and
`flush` must all happen on the same thread (typically the engine's
update thread). `Bus<EventT>` holds no lock.

## Caveats

- Subscribing from inside a callback is safe. A listener for the type being
  dispatched joins once that dispatch unwinds, so it hears neither the event
  being delivered nor the rest of that batch; one for another type joins at
  once, and hears that type's batch if this flush has not delivered it yet.
- A listener that enqueues an event sees it fire on the *next* flush - the
  next tick's, or the frame's own when no tick is left - whatever type it is,
  its own or another's. `flush` takes every bus's queue
  aside before it delivers any of them, so there is no bus left undrained for a
  re-entrant enqueue to slip into - and the answer does not depend on the order
  `TypeRegistry` hands the buses over, which is process-wide first-touch
  type-id order and differs between hosts.
- Listeners are iterated by index against a bound frozen when each bus's
  delivery starts; a listener that takes a long time to run will block subsequent
  listeners for the same event.
- A listener that calls `flush()` is ignored rather than refused. The flush
  already running is delivering everything there is, so a nested one has no
  work of its own - and letting it run would take every queue aside a second
  time, including the batch its own caller is being walked out of.
- A listener that throws ends that dispatch: the exception reaches whoever
  called `emit` or `flush`, and the rest of that delivery is lost - for a
  `flush`, the whole remaining flush, because every queue was taken aside
  before the first was delivered, so the events of every type not yet reached
  go with it. The bus itself is left working - the next subscribe joins at once and the next flush
  delivers. A behavior's `subscribe()` catches and reports its own
  listener's throw, as it does a hook's, so gameplay never gets that far.

---

## How it works inside

Everything above is what a project writes. What follows is how the
engine answers it, for whoever maintains that half.

### Implementation Notes

Internally each event type gets a lazily-created `Bus<EventT>` (stored
in `m_buses` keyed by `typeId<EventT>()`). The bus holds:

- a `std::vector<Entry>` of listeners (id, `std::function`, `alive`);
- a `std::vector<Entry>` of listeners subscribed mid-dispatch, admitted when it unwinds;
- two `std::vector<EventT>` buffers, swapped at flush start;
- an `m_flushDepth` counter deferring subscribe and unsubscribe.

`emit` and `flush` walk the listener vector by index, holding the size
constant so subscribes during dispatch don't grow the iteration. The
queue is swapped (not copied) at flush start.

### Key files

- `src/engine/core/event/event_bus.h` - `EventBus` (the facade)
- `src/engine/core/event/event_bus.cpp` - `flush()`
- `src/engine/core/event/bus.h` - per-type `Bus<EventT>` internals
