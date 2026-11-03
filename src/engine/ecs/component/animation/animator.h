#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "base/reflect/reflect.h"

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
 * Read whenever the pose is composed, a running frame included, so a bone can follow what moves every frame.
 * docs/reference/animation.md, "Adjusting a bone from gameplay".
 */
struct BoneAdjust {
    int32_t   bone     = -1;                     ///< Into the rig's bones; out of range adjusts nothing.
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  ///< Model space, about the bone's origin.
    glm::vec3 offset{0.0f};                      ///< Model space, added to the bone's origin.
    float     scale    = 1.0f;                   ///< Multiplies the bone's scale; children inherit.
    bool      absolute = false;                  ///< True: `rotation` replaces the clip's orientation.
};

/**
 * @brief What drives a rig: the skeleton, the clip playing on it, and the playback position.
 *
 * One per character, on the rig: import spawns a sub-entity per mesh, and a pose per mesh would be
 * clocks drifting apart. A mesh is skinned when its MeshAsset carries skin weights; which rig it takes is
 * docs/reference/animation.md, "Animator".
 *
 * Playback and blend state are not serialized: a saved scene reloads as the clip it was blending to.
 */
struct Animator {
    SkeletonHandle      skeleton;  ///< The rig posed; nothing is posed without it.
    AnimationClipHandle clip;      ///< Clip playing on it; empty holds the bind pose.

    float time    = 0.0f;  ///< Playback head in seconds; session state, not serialized. A start phase a game
                           ///< wants is set by the behavior that spawns the character.
    float speed   = 1.0f;  ///< Multiplies the simulation delta.
    bool  looping = true;  ///< Whether the clip in `clip` wraps at its end or stops there. crossFadeTo parks
                           ///< the outgoing clip's answer on `fadeLooping`.

    bool playOnStart = true;  ///< Start playing on the first frame the simulation runs. The only one of
                              ///< playOnStart / playing / started that is serialized; see decisions.md,
                              ///< "Authored state and session state on one component".

    bool playing = false;  ///< Session state.

    bool started = false;  ///< Whether playOnStart has been honored this session. Session state.

    AnimationClipHandle fadeFrom;              ///< Clip being left; empty when nothing is fading.
    float               fadeTime      = 0.0f;  ///< Its own playback head; it keeps playing while it fades.
    float               fadeRemaining = 0.0f;  ///< Simulation seconds of blend left.
    float               fadeDuration  = 0.0f;  ///< fadeRemaining's start; the weight divides by it.

    bool                fadeLooping   = true;  ///< Whether the outgoing clip wraps while it fades out; parked
                                               ///< here by crossFadeTo.

    std::vector<BoneAdjust> adjust;  ///< Per-bone additions on top of the clip; session state. Read by
                                     ///< composePose whenever the pose is composed, including frames the
                                     ///< behavior does not run (a paused editor), and never cleared: write an
                                     ///< identity or drop the entry. Entries naming one bone compose in list
                                     ///< order.

    /**
     * @brief Start playing @p clip, blending out of whatever is playing now.
     *
     * The outgoing clip keeps advancing while it fades; the blend runs in simulation seconds, unscaled
     * by `speed`. A call mid-fade back to the clip on its way out turns the fade round: that clip resumes
     * at its own head from the weight it had. Any other call mid-fade drops the clip on its way out, so
     * the pose jumps by its remaining weight. The countdown runs on simulation time, not on `playing`:
     * a one-shot shorter than the blend into it stops, and the fade still finishes on its last frame.
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
    VKM_F(skeleton, "The rig it poses")
    VKM_F(clip, "The clip playing on it; none holds the bind pose")
    VKM_F(speed, -10.0f, 10.0f, "Multiplies the simulation's time")
    VKM_F(playOnStart, "Starts by itself on Play, never while a scene is only open")
    VKM_F(looping, "Wraps at the clip's end rather than stopping")
VKM_REFLECT_END()
