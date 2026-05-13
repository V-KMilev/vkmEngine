#include "system/event/event_system.h"

namespace Engine {

void EventSystem::update(FrameContext&) {
    for (auto& bus : m_buses) {
        if (bus) bus->flush();
    }
}

} // namespace Engine
