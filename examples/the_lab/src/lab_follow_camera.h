#pragma once

#include <glm/glm.hpp>

#include "system/script/behavior.h"

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief The camera over this player's character: it orbits a point above them, turned with the
 *        right mouse button, and pulls in rather than looking through a wall.
 *
 * Only the character this end's player drives steers the scene's one camera. The engine puts the
 * camera's turn on the input command (InputCommand::view), so a tick moves and aims by the view the
 * player saw. Shots leave from the point it follows (`pivotHeight`; see LabShooter).
 */
class LabFollowCamera : public Behavior<LabFollowCamera> {
    public:
        void onUpdate(float dt) override;

    public:
        float followDistance  = 4.5f;
        float pivotHeight     = 1.0f;
        float followLag       = 8.0f;
        float lookSensitivity = 0.0035f;
        float minPitch        = -70.0f;
        float maxPitch        = 60.0f;

    private:
        /**
         * @brief How far back the camera can sit along @p back from @p pivot before scenery is in
         *        the way.
         *
         * @param pivot Where it orbits.
         * @param back  Unit direction from the pivot to the camera.
         * @return Meters, up to followDistance.
         */
        float clearDistance(const glm::vec3& pivot, const glm::vec3& back);

    private:
        float     m_yaw    = 0.0f;   ///< Where the player has turned the camera
        float     m_pitch  = 0.0f;
        float     m_reach  = 0.0f;   ///< How far back it sits now, easing out after a wall
        glm::vec3 m_pivot{0.0f};     ///< The point orbited, chasing the body
        bool      m_framed = false;  ///< Orbit seeded yet
};

} // namespace Lab

VKM_BEHAVIOR(::Lab::LabFollowCamera)
    VKM_F(followDistance, "How far the camera orbits from the pivot, meters.")
    VKM_F(pivotHeight, "The pivot's height above the character's origin, meters.")
    VKM_F(followLag, "How fast the pivot catches up, per second.")
    VKM_F(lookSensitivity, "Radians of orbit per pixel.")
    VKM_F(minPitch, "Lowest the camera may look, degrees.")
    VKM_F(maxPitch, "Highest, degrees.")
VKM_REFLECT_END()
