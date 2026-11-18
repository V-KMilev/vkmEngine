#pragma once

#include "ecs/entity.h"
#include "resource/asset_ref.h"
#include "resource/asset/animation_clip_asset.h"
#include "system/script/behavior.h"

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief Plays a rig's clips one after another, for ever, crossfading between them: a character
 *        to stand and watch.
 *
 * Drives the Animator at or under its entity. Each clip loops for `holdSeconds` before the next
 * fades in; an empty clip is skipped.
 */
class LabClipReel : public Behavior<LabClipReel> {
    public:
        void onUpdate(float dt) override;

    public:
        AssetRef<AnimationClipAsset> idleClip;
        AssetRef<AnimationClipAsset> walkClip;
        AssetRef<AnimationClipAsset> runClip;
        AssetRef<AnimationClipAsset> jumpClip;

        float holdSeconds = 3.0f;
        float fadeSeconds = 0.35f;

    private:
        /**
         * @brief Crossfade to the next clip that names one, after the one playing.
         */
        void playNext();

    private:
        int   m_playing = -1;    ///< Index into the clips in field order; -1 before the first
        float m_left    = 0.0f;  ///< Seconds until the next clip
};

} // namespace Lab

VKM_BEHAVIOR(::Lab::LabClipReel)
    VKM_F(idleClip)
    VKM_F(walkClip)
    VKM_F(runClip)
    VKM_F(jumpClip)
    VKM_F(holdSeconds, "How long each clip plays.")
    VKM_F(fadeSeconds, "Crossfade length between clips.")
VKM_REFLECT_END()
