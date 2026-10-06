#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/entity.h"
#include "resource/asset_ref.h"
#include "resource/asset/animation_clip_asset.h"
#include "system/script/reflected_behavior.h"

namespace Vkm::Engine {
    class Scene;
}

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief The characters this scene was authored with, in a fixed order.
 *
 * Players are handed one of these, not one built at join time: a character
 * built on the server after load would exist there only, and the client would
 * draw nothing. Authored ones sit at the same slots on both ends.
 *
 * @param scene Scene to search; a character has a CharacterController and a ScriptComponent.
 * @return The characters, sorted by slot so every machine and run agrees.
 */
std::vector<EntityId> authoredCharacters(Scene& scene);

/**
 * @brief Whether @p entity is the character the player drives when there is no session.
 *
 * Offline, isMine() and isSimulated() are true for every entity, so without a
 * seat every character would move as one. The seat is the first authored one.
 *
 * @param scene Scene holding the characters.
 * @param entity Entity to ask about.
 * @return True for the first authored character only.
 */
bool isOfflineSeat(Scene& scene, EntityId entity);

/**
 * @brief Drives the lab's character: input to movement, movement to animation.
 *
 * Writes camera-relative intent; CharacterControllerSystem decides the motion.
 * Clips are authored references so the cooker bakes them.
 */
class LabWalker : public ReflectedBehavior<LabWalker> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;
        void onFixedUpdate(float dt) override;

    public:
        /**
         * @brief Speed with nothing held, in metres per second.
         */
        float runSpeed = 4.0f;

        /**
         * @brief Speed while the walk key is held, in metres per second.
         *
         * Matches the walk clip (1.8m over 1.03s) so the feet do not skate.
         */
        float walkSpeed = 1.75f;

        float turnSpeed   = 12.0f;  ///< Turn rate toward travel, rad/s
        float fadeSeconds = 0.15f;  ///< Crossfade length between clips

        /**
         * @brief How far the camera orbits from the character, in metres.
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
         * @brief Whether this walker stands idle because there is no session.
         *
         * Offline the ownership questions cannot pick one walker out; isOfflineSeat
         * does. With a session open this is always false.
         *
         * @return True offline for every walker but the seat.
         */
        bool isUnseatedOffline();

        /**
         * @brief Crossfade to @p clip unless it is already the one playing.
         *
         * @param clip Name of the clip to play; ignored when empty or unknown.
         */
        void play(const std::string& clip);

        /**
         * @brief Pick the clip from what the character is doing.
         *
         * Reads the body's replicated motion, not input, so it works for every
         * walker. Airborne beats moving, and moving beats standing.
         */
        void chooseClip();

        /**
         * @brief Cast a ray from the eye, with every player where this one saw them.
         *
         * Lag-compensated: NetSession::Rewind puts the players back to what the
         * shooter saw for the length of the call.
         */
        void probe();

        /**
         * @brief Orbit the camera around the character and aim it at them.
         *
         * The pivot follows position, never facing: a camera that trailed facing
         * would move "forward" under the player's hand mid-step.
         *
         * @param dt Frame delta, seconds.
         */
        void followCamera(float dt);

    private:
        EntityId m_animator{};
        std::string m_playing;
        bool m_offlineSeat = false;   ///< See isOfflineSeat

        float m_orbitYaw = 0.0f;      ///< Where the player has turned the camera
        float m_orbitPitch = 0.0f;
        glm::vec3 m_pivot{0.0f};      ///< The point orbited, chasing the body
        bool m_framed = false;        ///< Orbit seeded yet
};

} // namespace Lab

VKM_REFLECT_BEGIN(::Lab::LabWalker)
    VKM_F(runSpeed)
    VKM_F(walkSpeed)
    VKM_F(turnSpeed)
    VKM_F(fadeSeconds)
    VKM_F(followDistance)
    VKM_F(aimHeight)
    VKM_F(followLag)
    VKM_F(lookSensitivity)
    VKM_F(minPitch)
    VKM_F(maxPitch)
    VKM_F(idleClip)
    VKM_F(walkClip)
    VKM_F(runClip)
    VKM_F(jumpClip)
VKM_REFLECT_END()
