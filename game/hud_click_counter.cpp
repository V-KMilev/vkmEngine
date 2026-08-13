#include "hud_click_counter.h"

#include <string>

#include "ecs/scene.h"
#include "ecs/component/ui_text.h"
#include "system/ui/ui_events.h"

namespace Engine {

void HudClickCounter::onStart() {
    // Auto-unsubscribes when this behavior is destroyed (engine/play stop or
    // hot-reload), so there is nothing to clean up by hand.
    subscribe<UIClickEvent>([this](const UIClickEvent&) {
        ++m_count;
        Scene& scene = *context().scene;
        if (scene.has<UIText>(m_entity)) {
            scene.get<UIText>(m_entity).text = "Clicks: " + std::to_string(m_count);
        }
    });
}

} // namespace Engine
