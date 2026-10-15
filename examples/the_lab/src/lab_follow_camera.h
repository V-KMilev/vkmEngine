#pragma once

#include <glm/glm.hpp>

#include "system/script/reflected_behavior.h"

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief The camera over this player's character: it orbits a point above them, turned with the
 *        right mouse button, and pulls in rather than looking through a wall.
 *
 * Only the character this end's player drives steers the scene's one camera. The camera's turn
 * rides the input command (InputMap::setView), so a tick moves and aims by the view the player
 * saw. Shots leave from the point it follows (`pivotHeight`; see LabShooter).
 */
class LabFollowCamera : public ReflectedBehavior<LabFollowCamera> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;

    public:
        float followDistance  = 4.5f;     ///< How far the camera orbits from the pivot, metres.
        float pivotHeight     = 1.0f;     ///< The pivot's height above the character's origin, metres.
        float followLag       = 8.0f;     ///< How fast the pivot catches up, per second.
        float lookSensitivity = 0.0035f;  ///< Radians of orbit per pixel.
        float minPitch        = -70.0f;   ///< Lowest the camera may look, degrees.
        float maxPitch        =  60.0f;   ///< Highest, degrees.

    private:
        /**
         * @brief How far back the camera can sit along @p back from @p pivot before scenery is in
         *        the way.
         *
         * @param pivot Where it orbits.
         * @param back  Unit direction from the pivot to the camera.
         * @return Metres, up to followDistance.
         */
        float clearDistance(const glm::vec3& pivot, const glm::vec3& back);

    private:
        float     m_yaw   = 0.0f;   ///< Where the player has turned the camera
        float     m_pitch = 0.0f;
        float     m_reach = 0.0f;   ///< How far back it sits now, easing out after a wall
        glm::vec3 m_pivot{0.0f};    ///< The point orbited, chasing the body
        bool      m_framed = false; ///< Orbit seeded yet
};

} // namespace Lab

VKM_REFLECT_BEGIN(::Lab::LabFollowCamera)
    VKM_F(followDistance)
    VKM_F(pivotHeight)
    VKM_F(followLag)
    VKM_F(lookSensitivity)
    VKM_F(minPitch)
    VKM_F(maxPitch)
VKM_REFLECT_END()
