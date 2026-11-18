#pragma once

#include <string>

#include <glm/glm.hpp>

#include "ecs/entity.h"
#include "system/script/behavior.h"

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief A station's name and what it shows, pinned over its entity while the camera is near.
 *
 * The engine has no world-space UI, so the label is screen-space text placed each frame where
 * the entity's point lands on its canvas (ScreenView::toCanvas). It fades out between
 * `fadeFrom` and `showWithin` meters from the camera, and while scenery stands between the camera
 * and it. It is hidden while the game is paused, and on a host that draws nothing it is never
 * built.
 */
class LabSign : public Behavior<LabSign> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;
        void onRealtimeUpdate(float dt) override;

    public:
        std::string title;
        std::string caption;

        float height     = 2.4f;
        float showWithin = 18.0f;
        float fadeFrom   = 12.0f;

    private:
        /**
         * @brief Add a line of text to the label, centered and wrapped.
         *
         * @param name Entity name of the line.
         * @param text What it says.
         * @param size Its height, in canvas pixels.
         * @param ink  Its color.
         * @param y    How far down the label it starts, in canvas pixels.
         * @return The line.
         */
        EntityId addLine(
            const char* name,
            const std::string& text,
            float size,
            const glm::vec4& ink,
            float y
        );

        /**
         * @brief Whether scenery stands between the camera at @p eye and @p point.
         *
         * @param eye   The camera's position.
         * @param point Where the label hangs.
         * @return True when a static or kinematic body is in the way.
         */
        bool isHidden(const glm::vec3& eye, const glm::vec3& point);

    private:
        EntityId m_canvas{};
        EntityId m_label{};
        float    m_shown = 0.0f;  ///< Opacity now, easing toward what the distance and walls allow
};

} // namespace Lab

VKM_BEHAVIOR(::Lab::LabSign)
    VKM_F(title, "The station's name, large.")
    VKM_F(caption, "What there is to see, under it.")
    VKM_F(height, "Meters above the entity the label hangs at.")
    VKM_F(showWithin, "Meters from the camera past which it is gone.")
    VKM_F(fadeFrom, "Meters from the camera inside which it is whole.")
VKM_REFLECT_END()
