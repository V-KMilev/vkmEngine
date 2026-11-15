#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/entity.h"
#include "io/project/render_settings.h"
#include "system/script/behavior.h"
#include "ecs/component/ui/ui_build.h"

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief Esc opens a menu over the frozen lab: Resume, Restart, and a page of graphics settings.
 *
 * Built once while time runs, as a canvas that pauses the game: its action opens and closes it,
 * and while it shows the world stands still (docs/reference/ui.md). A setting applies as it is
 * changed: the quality tier is Graphics::setQuality, and the rows under it refine what the tier
 * chose; the last switches the window between fullscreen and windowed (Graphics::setWindowMode).
 * The page opens on, and after each change shows, what the frame renders.
 */
class LabPauseMenu : public Behavior<LabPauseMenu> {
    public:
        void onStart() override;
        void onRealtimeUpdate(float dt) override;

    public:
        float fadeSeconds = 0.18f;

    private:
        /// Hide the menu, which lets the world run on.
        void close();

        /**
         * @brief Show the settings page in place of the main one, or the main one back.
         *
         * @param settings Whether the settings page is the one shown.
         */
        void showSettings(bool settings);

        /**
         * @brief The settings column's next row, stepping through @p options: "< option >" on its right.
         *
         * @param name    What the setting is called, on the row's left.
         * @param options What it can be, in step order.
         * @return The choice.
         */
        EntityId addChoice(const char* name, std::vector<std::string> options);

        /**
         * @brief The settings column's next row, on or off: a switch on its right.
         *
         * @param name What the setting is called, on the row's left.
         * @return The toggle.
         */
        EntityId addToggle(const char* name);

        /**
         * @brief Set every row to what the frame renders with, and the tier to the one in effect.
         */
        void showInEffect();

        /**
         * @brief Set every row under the tier to @p settings.
         *
         * @param settings What the frame renders with.
         */
        void showRows(const RenderSettings& settings);

        /**
         * @brief Render as the rows say; an effect the project has off stays off.
         */
        void apply();

    private:
        UI::Style m_style;  ///< The menu's look, set in onStart.

        EntityId m_canvas{};
        EntityId m_menu{};
        EntityId m_main{};
        EntityId m_settings{};
        EntityId m_rows{};  ///< The settings page's column, which lays its rows out
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
        EntityId m_fullscreen{};
};

} // namespace Lab

VKM_BEHAVIOR(::Lab::LabPauseMenu)
    VKM_F(fadeSeconds, "How long the menu takes to fade in.")
VKM_REFLECT_END()
