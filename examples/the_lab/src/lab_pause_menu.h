#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/entity.h"
#include "system/render/render_settings.h"
#include "system/script/reflected_behavior.h"

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief Esc opens a menu over the frozen lab: Resume, Restart, and a page of graphics settings.
 *
 * Built once while time runs, then driven from onRealtimeUpdate - the only hook still running
 * once it has paused the world. See docs/reference/scripting.md. A setting applies as it is
 * changed: the quality tier is withPreset over the project's settings, and the rows under it
 * refine what the tier chose.
 */
class LabPauseMenu : public ReflectedBehavior<LabPauseMenu> {
    public:
        void onStart() override;
        void onRealtimeUpdate(float dt) override;

    public:
        float fadeSeconds = 0.18f;  ///< How long the menu takes to fade in.

    private:
        /**
         * @brief Show or hide the menu, pausing the world to match.
         *
         * @param open Whether the menu is up.
         */
        void setOpen(bool open);

        /**
         * @brief Show the settings page in place of the main one, or the main one back.
         *
         * @param settings Whether the settings page is the one shown.
         */
        void showSettings(bool settings);

        /**
         * @brief A button with a centred caption.
         *
         * @param parent  Page it sits on.
         * @param caption What it says.
         * @param eventId What its click is called.
         * @param at      Its top middle, from the page's top middle, in canvas pixels.
         * @param size    Its size, in canvas pixels.
         * @return The button.
         */
        EntityId addButton(
            EntityId parent,
            const char* caption,
            const std::string& eventId,
            const glm::vec2& at,
            const glm::vec2& size
        );

        /**
         * @brief A row of the settings page with the setting's name on its left.
         *
         * @param name What the setting is called.
         * @param y    Its top, in the page's canvas pixels.
         * @return The row.
         */
        EntityId addRow(const char* name, float y);

        /**
         * @brief A settings row stepping through @p options: "< option >" on its right.
         *
         * @param name    What the setting is called.
         * @param eventId What a step of it is called.
         * @param options What it can be, in step order.
         * @param y       Its top, in the page's canvas pixels.
         * @return The choice.
         */
        EntityId addChoice(
            const char* name,
            const std::string& eventId,
            std::vector<std::string> options,
            float y
        );

        /**
         * @brief A settings row that is on or off: a switch on its right.
         *
         * @param name    What the setting is called.
         * @param eventId What a flip of it is called.
         * @param y       Its top, in the page's canvas pixels.
         * @return The toggle.
         */
        EntityId addToggle(const char* name, const std::string& eventId, float y);

        /**
         * @brief Set every row under the tier to what @p preset renders with.
         *
         * @param preset The tier.
         */
        void showTier(GraphicsPreset preset);

        /**
         * @brief Render as the rows say: the chosen tier over the project's settings, refined by each row.
         */
        void apply();

    private:
        EntityId m_canvas{};
        EntityId m_menu{};
        EntityId m_main{};
        EntityId m_settings{};
        bool     m_open = false;
        float    m_fade = 0.0f;

        EntityId m_quality{};
        EntityId m_antiAliasing{};
        EntityId m_shadows{};
        EntityId m_renderScale{};
        EntityId m_bloom{};
        EntityId m_occlusion{};
        EntityId m_reflections{};
        EntityId m_softShadows{};
        EntityId m_culling{};
};

} // namespace Lab

VKM_REFLECT_BEGIN(::Lab::LabPauseMenu)
    VKM_F(fadeSeconds)
VKM_REFLECT_END()
