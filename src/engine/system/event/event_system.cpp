#include "system/event/event_system.h"

#include "debug/profiler.h"

namespace Engine {

void EventSystem::update(FrameContext&) {
    PROFILE_SCOPE("EventSystem");
    for (auto& bus : m_buses) {
        if (bus) bus->flush();
    }
}

} // namespace Engine
