#pragma once

#include "system/script/reflected_behavior.h"

namespace Engine {

/**
 * @brief Demo behavior: tallies UI clicks and shows the count in its own UIText.
 *
 * Attached to a UIText entity, it subscribes to the UI click bus on start and
 * rewrites its text whenever any button fires - a minimal demonstration of the
 * UI -> gameplay path (a Behavior reacting to a UIClickEvent). The running count
 * is internal state, so there are no reflected fields.
 */
class HudClickCounter : public ReflectedBehavior<HudClickCounter> {
    public:
        static constexpr const char* TYPE_NAME = "HudClickCounter";

        void onStart() override;

    private:
        int m_count = 0;
};

VKM_REFLECT_BEGIN(HudClickCounter)
VKM_REFLECT_END()

} // namespace Engine
