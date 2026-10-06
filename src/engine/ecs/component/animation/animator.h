#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/reflect.h"

#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/skeleton_asset.h"

namespace Vkm::Engine {

/**
 * @brief What a behavior adds to one bone on top of the clip, this frame.
 *
 * composePose applies it after the clip is sampled and the crossfade blended, before the bone is
 * composed, so the subtree follows. Rotation and offset are in rig model space about the bone's
 * origin; a caller in world axes turns the axis by the inverse of the rig's world rotation. The bone
 * is an index resolved against the Animator's skeleton: session state, never serialized.
 *
 * `scale` near zero collapses the bone's skin to a point; exactly zero leaves no orientation to read.
 * Read on the tick, so a bone following something that moves every frame steps at the tick rate.
 * docs/reference/animation.md, "Adjusting a bone from gameplay".
 */
struct BoneAdjust {
    int32_t   bone = -1;                         ///< Into the rig's bones; out of range adjusts nothing.
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  ///< Model space, about the bone's origin.
    glm::vec3 offset{0.0f};                      ///< Model space, added to the bone's origin.
    float     scale    = 1.0f;                   ///< Multiplies the bone's scale; children inherit.
    bool      absolute = false;                  ///< True: `rotation` replaces the clip's orientation.
};

/**
 * @brief What drives a rig: the skeleton, the clip playing on it, and the playback position.
 *
 * One per character, on the rig: import spawns a sub-entity per mesh, and a pose per mesh would be
 * clocks drifting apart. A mesh is skinned when its MeshAsset carries skin weights, and its rig is the
 * nearest Animator at or above it in the Hierarchy, so no EntityId needs remapping.
 *
 * Playback and blend state are not serialized: a saved scene reloads as the clip it was blending to.
 */
struct Animator {
    SkeletonHandle      skeleton;  ///< The rig posed; nothing is posed without it.
    AnimationClipHandle clip;      ///< Clip playing on it; empty holds the bind pose.

    /**
     * @brief Playback head in seconds; session state, not serialized.
     *
     * A start phase a game wants is set by the behavior that spawns the character.
     */
    float time    = 0.0f;
    float speed   = 1.0f;   ///< Multiplies the simulation delta.
    /**
     * @brief Whether the clip in `clip` wraps at its end or stops there.
     *
     * crossFadeTo parks the outgoing clip's answer on `fadeLooping`.
     */
    bool  looping = true;

    /**
     * @brief Start playing on the first frame the simulation runs.
     *
     * The only one of playOnStart / playing / started that is serialized; see engine.md,
     * "Authored state and session state on one component".
     */
    bool playOnStart = true;

    /// Session state.
    bool playing = false;

    /// Whether playOnStart has been honoured this session. Session state.
    bool started = false;

    AnimationClipHandle fadeFrom;              ///< Clip being left; empty when nothing is fading.
    float               fadeTime      = 0.0f;  ///< Its own playback head; it keeps playing while it fades.
    float               fadeRemaining = 0.0f;  ///< Simulation seconds of blend left.
    float               fadeDuration  = 0.0f;  ///< fadeRemaining's start; the weight divides by it.

    /**
     * @brief Whether the outgoing clip wraps while it fades out; parked here by crossFadeTo.
     */
    bool                fadeLooping   = true;

    /**
     * @brief Per-bone additions on top of the clip; session state.
     *
     * Read by composePose whenever the pose is composed, including frames the behavior does not run (a
     * paused editor), and never cleared: write an identity or drop the entry. Entries naming one bone
     * compose in list order.
     */
    std::vector<BoneAdjust> adjust;

    /**
     * @brief Start playing @p clip, blending out of whatever is playing now.
     *
     * The outgoing clip keeps advancing while it fades; the blend runs in simulation seconds, unscaled
     * by `speed`. A call mid-fade drops the clip on its way out, so the pose jumps by its remaining weight.
     *
     * @param animator Animator to retarget, in place.
     * @param clip Clip to play; the same clip already playing is left alone, a stopped one plays again.
     * @param seconds Blend length; zero or less is a cut, as is having no clip to blend out of.
     * @param looping Whether @p clip wraps; taken here so the outgoing clip keeps the answer it played under.
     */
    static void crossFadeTo(Animator& animator, AnimationClipHandle clip, float seconds, bool looping);
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::Animator)
    VKM_F(skeleton)
    VKM_F(clip)
    VKM_F(speed)
    VKM_F(playOnStart)
    VKM_F(looping)
VKM_REFLECT_END()
