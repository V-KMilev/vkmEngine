# Event Bus

Typed pub/sub for in-engine events. A subscriber registers a callback for one
event type; a publisher calls `emit()` to deliver now or `enqueue()` to deliver
at the next `flush()`.

The bus is engine infrastructure, not a System: the Engine owns one, every
`FrameContext` carries it as `ctx.events`, and `Engine::run` calls `flush()` at
the top of the Simulation stage once per tick, and once more per frame for
whatever was queued outside a tick. Delivery is per tick so a reaction's delay
does not depend on the frame rate.

## Event types

Any struct is an event - no base class, no registration. The type is the
channel: a `DamageEvent` subscriber sees only `DamageEvent`s.

```cpp
struct DamageEvent { EntityId target; int amount; };
```

## Subscribing and publishing

```cpp
// In a system: ctx.events. From the app layer: engine.getEvents().
ListenerId id = ctx.events.subscribe<DamageEvent>([](const DamageEvent& e) {
    applyDamage(e.target, e.amount);
});
ctx.events.unsubscribe<DamageEvent>(id);   // true if it was there

ctx.events.emit(DamageEvent{target, 50});     // every listener runs now
ctx.events.enqueue(DamageEvent{target, 25});  // runs at the next flush()
```

Use `emit` for a reaction that belongs in the same tick; `enqueue` for
decoupled flow (UI, asset events, anything latency-tolerant). An event enqueued
by a listener during a flush - of any type - is delivered at the *next* flush:
every queue is taken aside before any delivers.

A behavior does not get the bus. `Behavior::events()` is an `EventSender` (emit
and enqueue only), and it listens through its own tracked `subscribe()`, which
is dropped when the session ends; see [Scripting](scripting.md). A listener
registered on the bus directly would outlive the gameplay module, and a flush
after a script reload would call unmapped code.

## Contracts

- **Main thread only.** Every call happens on the frame thread; nothing locks.
- **Subscribe and unsubscribe work inside a listener**, including on itself. An
  unsubscribed listener stops at once and is removed when the dispatch unwinds.
  A listener added for the type being dispatched joins after it, hearing none of
  the current batch; one for another type joins at once.
- **A nested `flush()` is ignored**; the running flush delivers everything.
- **A throwing listener ends that dispatch**: the exception reaches the caller
  of `emit` or `flush`, and the rest of that delivery - for a `flush`, every
  type not yet reached - is lost. The bus keeps working. A behavior's
  `subscribe()` catches and reports its listener's throw.
- **A script reload drops idle buses.** `ScriptModule` calls
  `EventBus::dropIdleBuses` before unmapping the old library, since a bus for a
  module's event type holds that module's code.

## Key files

- `src/engine/core/event/event_bus.h` - `EventBus` and `EventSender`
- `src/engine/core/event/event_bus.cpp` - `flush()`
- `src/engine/core/event/bus.h` - per-type `Bus<EventT>`
