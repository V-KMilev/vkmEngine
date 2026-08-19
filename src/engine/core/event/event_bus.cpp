#include "core/event/event_bus.h"

#include <vector>

#include "debug/profiler.h"

namespace Vkm::Engine {

void EventBus::flush() {
    PROFILE_SCOPE("EventBus::flush");
    // Snapshot the bus pointers before flushing: a listener fired during flush()
    // may enqueue an event of a never-before-seen type, which lazily creates a
    // new bus and can reallocate m_buses - invalidating a live iterator over it.
    // The Bus objects are heap-stable (held by unique_ptr), so raw pointers
    // captured here stay valid; any bus created mid-flush simply flushes next
    // frame (matching the documented enqueue-from-listener contract).
    std::vector<IBus*> active;
    active.reserve(m_buses.size());
    for (auto& bus : m_buses) {
        if (bus) active.push_back(bus.get());
    }
    for (IBus* bus : active) {
        bus->flush();
    }
}

} // namespace Vkm::Engine
