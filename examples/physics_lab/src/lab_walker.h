#pragma once

#include <string>

#include <glm/glm.hpp>

#include "ecs/entity.h"
#include "resource/asset_ref.h"
#include "resource/asset/animation_clip_asset.h"
#include "system/script/reflected_behavior.h"

namespace Vkm::Engine {

/**
 * @brief Drives the lab's character: input to movement, movement to animation.
 *
 * Deliberately thin. Everything deciding how the character behaves lives in the
 * engine - CharacterControllerSystem turns moveInput into velocity, resolves
 * ground, and mounts what it can - so this writes intent and reads back what
 * happened. A behavior reimplementing any of that would be testing itself
 * rather than the engine, which is the opposite of what a lab is for.
 *
 * Movement is camera-relative, because a course you walk around is unusable if
 * "forward" means the world's +Z rather than the way you are looking.
 *
 * The clips are authored rather than found by name in code: they are what the
 * scene references, and what the scene references is what the cooker bakes.
 */
class LabWalker : public ReflectedBehavior<LabWalker> {
    public:
        static constexpr const char* TYPE_NAME = "LabWalker";

        void onStart() override;
        void onUpdate(float dt) override;

    public:
        /**
         * @brief Speed with nothing held, in metres per second.
         *
         * Running is the default because crossing the course at a walk is
         * tedious, and a lab nobody crosses tests nothing. The walk key is the
         * exception rather than the rule for the same reason.
         */
        float runSpeed = 4.0f;

        /**
         * @brief Speed while the walk key is held, in metres per second.
         *
         * Matches what the walk clip was authored at - 1.8m of travel over
         * 1.03s - so the feet meet the ground instead of skating. An in-place
         * clip has no opinion about speed, so the two are only ever as matched
         * as someone makes them.
         */
        float walkSpeed = 1.75f;

        float turnSpeed   = 12.0f;  ///< Turn rate toward travel, rad/s
        float fadeSeconds = 0.15f;  ///< Crossfade length between clips

        /**
         * @brief How far the camera orbits from the character, in metres.
         *
         * Fixed. The camera turns around the character rather than trailing
         * its facing, which is also what breaks a loop: movement is
         * camera-relative, so a camera that followed the character's facing
         * moved the meaning of "forward" every time the character turned.
         */
        float followDistance = 4.5f;

        float aimHeight = 1.0f;   ///< Pivot height above the character's origin
        float followLag = 8.0f;   ///< How fast the pivot catches up, per second

        float lookSensitivity = 0.0035f;  ///< Radians of orbit per pixel
        float minPitch = -70.0f;          ///< Lowest the camera may look, degrees
        float maxPitch =  60.0f;          ///< Highest, degrees

        AssetRef<AnimationClipAsset> idleClip;  ///< Played standing still
        AssetRef<AnimationClipAsset> walkClip;  ///< Played while walking
        AssetRef<AnimationClipAsset> runClip;   ///< Played while moving otherwise
        AssetRef<AnimationClipAsset> jumpClip;  ///< Played while airborne

    private:
        /**
         * @brief The descendant carrying the Animator, or an invalid id.
         *
         * A model import puts the Animator on the entity whose frame the rig's
         * bones are composed in, which is a child of the entity the character's
         * physics lives on - so the two are never the same entity, and the
         * behavior has to go and find it.
         *
         * @param root Entity to search from, inclusive.
         * @return The entity holding the Animator, or an invalid id.
         */

        /**
         * @brief Crossfade to @p clip unless it is already the one playing.
         *
         * @param clip Name of the clip to play; ignored when empty or unknown.
         */
        void play(const std::string& clip);

        /**
         * @brief Orbit the camera around the character and aim it at them.
         *
         * The angle is the player's and the distance is fixed; only the pivot
         * follows, and it follows a position rather than a facing. That is what
         * keeps camera-relative movement stable - a camera that trailed the
         * character's facing turned every time the character did, which moved
         * the direction of "forward" under the player's hand mid-step.
         *
         * @param dt Frame delta, seconds.
         */
        void followCamera(float dt);

    private:
        EntityId m_animator{};
        std::string m_playing;

        float m_orbitYaw = 0.0f;      ///< Where the player has turned the camera
        float m_orbitPitch = 0.0f;
        glm::vec3 m_pivot{0.0f};      ///< The point orbited, chasing the body
        bool m_framed = false;        ///< Whether the orbit has been seeded yet
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::LabWalker)
    VKM_F(runSpeed),
    VKM_F(walkSpeed),
    VKM_F(turnSpeed),
    VKM_F(fadeSeconds),
    VKM_F(followDistance),
    VKM_F(aimHeight),
    VKM_F(followLag),
    VKM_F(lookSensitivity),
    VKM_F(minPitch),
    VKM_F(maxPitch),
    VKM_F(idleClip),
    VKM_F(walkClip),
    VKM_F(runClip),
    VKM_F(jumpClip)
VKM_REFLECT_END()
