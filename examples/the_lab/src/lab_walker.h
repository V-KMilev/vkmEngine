#pragma once

#include <string>

#include <glm/glm.hpp>

#include "ecs/entity.h"
#include "resource/asset_ref.h"
#include "resource/asset/animation_clip_asset.h"
#include "system/script/behavior.h"

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief Drives the lab's character: input to movement, movement to animation, and what the
 *        player does with their feet and hands - use a switch, bounce off a jump pad, get knocked
 *        down and stand up again.
 *
 * Writes camera-relative intent; CharacterControllerSystem decides the motion. The server decides
 * a press. Its siblings on the character shoot (LabShooter), throw (LabThrower) and hold the
 * camera (LabFollowCamera).
 */
class LabWalker : public Behavior<LabWalker> {
    public:
        void onStart() override;
        void onFixedUpdate(float dt) override;

        /**
         * @brief Hand the character to its ragdoll, pushed by @p momentum at @p at; the authority's alone.
         *
         * The bones nearest the blow take most of it, and the character stands again after
         * `standUpAfter`.
         *
         * @param momentum What struck it, kg m/s.
         * @param at       Where, in the world.
         */
        void knockDown(const glm::vec3& momentum, const glm::vec3& at);

    public:
        float runSpeed    = 4.0f;
        float walkSpeed   = 1.75f;
        float turnSpeed   = 12.0f;
        float fadeSeconds = 0.15f;

        AssetRef<AnimationClipAsset> idleClip;
        AssetRef<AnimationClipAsset> walkClip;
        AssetRef<AnimationClipAsset> runClip;
        AssetRef<AnimationClipAsset> jumpClip;

        float standUpAfter = 4.0f;

    private:
        /**
         * @brief Crossfade to @p clip unless it is already the one playing.
         *
         * @param clip The clip to play; ignored when it names none this session has.
         */
        void play(const AssetRef<AnimationClipAsset>& clip);

        /**
         * @brief Pick the clip from what the character is doing.
         *
         * Reads the body's replicated motion, not input, so it works for every
         * walker. Airborne beats moving, and moving beats standing.
         */
        void chooseClip();

        /// Press the nearest LabSwitch in reach; the authority's.
        void use();

        /// Launch from the LabJumpPad underfoot, if it has just landed on one.
        void bounce();

    private:
        std::string m_playing;    ///< The clip last faded to, by name
        EntityId    m_lastPad{};  ///< The jump pad underfoot last tick
};

} // namespace Lab

VKM_BEHAVIOR(::Lab::LabWalker)
    VKM_F(runSpeed, "Speed with nothing held, m/s")
    VKM_F(walkSpeed, "With the walk key held, m/s: the walk clip's, so feet do not skate")
    VKM_F(turnSpeed, "How fast it turns toward travel, per second")
    VKM_F(fadeSeconds, "Crossfade length between clips")
    VKM_F(idleClip, "Played standing still")
    VKM_F(walkClip, "Played while walking")
    VKM_F(runClip, "Played while moving otherwise")
    VKM_F(jumpClip, "Played while airborne")
    VKM_F(standUpAfter, "Seconds a knocked-down character lies there; 0 keeps it down.")
VKM_REFLECT_END()
