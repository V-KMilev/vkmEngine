#pragma once

#include "ecs/entity.h"
#include "system/script/reflected_behavior.h"

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief Esc opens a menu over the frozen lab; Resume and Restart get you out.
 *
 * Built once while time runs, then driven from onRealtimeUpdate - the only hook
 * still running once it has paused the world. See docs/reference/scripting.md.
 */
class LabPauseMenu : public ReflectedBehavior<LabPauseMenu> {
    public:
        void onStart() override;
        void onRealtimeUpdate(float dt) override;

    public:
        float fadeSeconds = 0.18f;  ///< How long the panel takes to fade in.

    private:
        /**
         * @brief Show or hide the menu, pausing the world to match.
         *
         * @param open Whether the menu is up.
         */
        void setOpen(bool open);

    private:
        EntityId m_canvas{};
        EntityId m_panel{};
        bool     m_open  = false;
        float    m_fade  = 0.0f;
};

} // namespace Lab

VKM_REFLECT_BEGIN(::Lab::LabPauseMenu)
    VKM_F(fadeSeconds)
VKM_REFLECT_END()
