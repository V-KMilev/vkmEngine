#include "core/event/event_bus.h"

#include "debug/profiler.h"

namespace Vkm::Engine {

void EventBus::flush() {
    PROFILE_SCOPE("EventBus::flush");

    // A nested flush would have takeQueue clear the batch the outer deliver is walking.
    if (m_flushing) return;
    m_flushing = true;

    // Cleared even if a listener throws, or every later flush would return here.
    struct FlushingScope {
        bool& flushing;
        ~FlushingScope() { flushing = false; }
    } scope{m_flushing};

    // Snapshot first: a listener enqueuing a new event type creates a bus and can
    // reallocate m_buses. The Bus objects are heap-stable, so the pointers stay valid.
    m_active.clear();
    m_buses.forEach([&](IBus& bus) { m_active.push_back(&bus); });

    for (IBus* bus : m_active) bus->takeQueue();
    for (IBus* bus : m_active) bus->deliver();
}

void EventBus::dropIdleBuses() {
    m_buses.removeIf([](const IBus& bus) { return !bus.hasListeners(); });
}

} // namespace Vkm::Engine
