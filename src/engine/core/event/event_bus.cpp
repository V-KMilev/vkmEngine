#include "core/event/event_bus.h"

#include <vector>

#include "debug/profiler.h"

namespace Vkm::Engine {

void EventBus::flush() {
    PROFILE_SCOPE("EventBus::flush");
    // Snapshot the bus pointers first: a listener may enqueue an event of a
    // never-before-seen type, which lazily creates a bus and can reallocate
    // m_buses. The Bus objects are heap-stable, so the pointers stay valid.
    std::vector<IBus*> active;
    active.reserve(m_buses.count());
    m_buses.forEach([&](IBus& bus) { active.push_back(&bus); });
    for (IBus* bus : active) {
        bus->flush();
    }
}

} // namespace Vkm::Engine
