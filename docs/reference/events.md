# Event Bus

Typed pub/sub for in-engine events. A subscriber registers a callback for one
event type; a publisher calls `emit()` to deliver now or `enqueue()` to deliver
at the next `flush()`.

The bus is engine infrastructure, not a System: the Engine owns one, every
`FrameContext` carries it as `ctx.events`, and `Engine::run` calls `flush()` at
the top of the Simulation stage once per tick, and once more per frame for
whatever was queued outside a tick. Delivery is per tick so a reaction's delay
does not depend on the frame rate.

## Subscribing and publishing

Any struct is an event - no base class, no registration. The type is the
channel: a `DamageEvent` subscriber sees only `DamageEvent`s.

```cpp
struct DamageEvent { EntityId target; int amount; };

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
every queue is taken aside before any delivers. A flush delivers type by type, in
the order of their ids (`typeId<T>()`), each type's events in the order they were
queued: events of two types do not arrive in the order they were queued.

A behavior does not get the bus: a listener on it would outlive the gameplay module,
and a flush after a script reload would call unmapped code
([Scripting](scripting.md#unloading-a-module)). `BehaviorBase::events()` is an
`EventSender` (emit and enqueue only), and it listens only through its own `subscribe()`,
dropped when the behavior goes or the session ends; what its listener's throw does is
[Scripting](scripting.md#behaviorsystem)'s.

## Contracts

- **Main thread only.** Every call happens on the frame thread; nothing locks.
- **Subscribe and unsubscribe work inside a listener**, including on itself. An
  unsubscribed listener stops at once and is removed when the dispatch unwinds.
  A listener added for the type being dispatched joins after it, hearing none of
  the current batch; one for another type joins at once.
- **A nested `flush()` is ignored**; the running flush delivers everything.
- **A throwing listener ends that dispatch**; `EventBus::emit` and
  `EventBus::flush` say what is lost.

## How it works inside

Each event type has a `Bus<EventT>`, made by its first subscribe or enqueue and kept in a
`TypeRegistry`. A bus holds its type's code, so a script reload drops every idle one before
the old library is unmapped (`EventBus::dropIdleBuses`).

## Key files

- `src/engine/base/event/event_bus.h` - `EventBus`, `EventSender` and the per-type `Bus<EventT>`
