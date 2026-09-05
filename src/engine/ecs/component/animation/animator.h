#pragma once

#include "core/reflect.h"

#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/skeleton_asset.h"

namespace Vkm::Engine {

/**
 * @brief What drives a rig: the skeleton, the clip playing on it, and where in
 *        that clip playback stands.
 *
 * One Animator per character, not one per mesh. Import spawns a sub-entity per
 * mesh, so a rigged character arrives as body plus clothes plus hair, and a pose
 * on the mesh would be three clocks drifting apart. The Animator sits on the rig
 * and the pose is published for the whole subtree beneath it.
 *
 * The rig is not a `SkinnedMesh` component either: a mesh is skinned exactly when
 * its MeshAsset carries skin weights, and the rig driving it is the nearest
 * Animator at or above it in the Hierarchy - the structure import produces
 * anyway. That relationship needs no EntityId in a serialized row, so it survives
 * prefabs, undo and scene load without a remap.
 *
 * The authored fields persist; playback and blend state do not. A crossfade is
 * two clips and a countdown, and a scene row freezing that shape would outlive
 * the blend system that wrote it - so a saved scene reloads as the clip it was
 * blending to, one fade early.
 */
struct Animator {
    SkeletonHandle      skeleton;  ///< The rig posed; nothing is posed without it.
    AnimationClipHandle clip;      ///< Clip playing on it; empty holds the bind pose.

    /**
     * @brief Playback head, seconds into the clip. Session state.
     *
     * Not serialized, for the reason `playing` is not: it describes what was
     * happening, not what was authored. Scrub the head in the Animator card,
     * press Ctrl+S for an unrelated reason, and the scene would hold a head
     * position nobody chose, with nothing to undo. A start phase a game wants -
     * two characters half a lap apart - is set by the behavior that spawns
     * them, which is where "they should not be in step" is decided.
     */
    float time    = 0.0f;
    float speed   = 1.0f;   ///< Playback multiplier applied to the simulation delta.
    /**
     * @brief Whether the clip in `clip` wraps at its end or stops there.
     *
     * The current clip's, not the animator's: a character that walks and then
     * dies plays one clip that loops and one that does not, and the flag moves
     * with the clip. crossFadeTo takes the incoming clip's and parks this one
     * on `fadeLooping` for the clip on its way out.
     */
    bool  looping = true;

    /**
     * @brief Start playing on the first frame the simulation runs.
     *
     * The authored half of the trio every component that plays something
     * carries; see engine.md, "Authored state and session state on one
     * component", for why only this one is serialized.
     */
    bool playOnStart = true;

    // Transient: runtime state, never serialized. A layer or blend-tree system
    // replaces the blend fields without touching the authored ones above.

    /// Whether the clip should be advancing right now. Session state.
    bool playing = false;

    /// Whether playOnStart has been honoured yet this session. Session state.
    bool started = false;

    AnimationClipHandle fadeFrom;            ///< Clip being left; empty when nothing is fading.
    float               fadeTime      = 0.0f;   ///< Its own playback head - it keeps playing while it fades.
    float               fadeRemaining = 0.0f;   ///< Simulation seconds of blend still to run.
    float               fadeDuration  = 0.0f;   ///< What it started at, which is what the weight is measured against.

    /**
     * @brief Whether the outgoing clip wraps while it fades out.
     *
     * `looping` belongs to the clip in `clip`, and the outgoing one may answer
     * differently - a one-shot fading out of a walk must clamp at its end
     * rather than start again for the half second the blend lasts. Parked here
     * by crossFadeTo, so the caller cannot get the order wrong by setting
     * `looping` first.
     */
    bool                fadeLooping   = true;

    /**
     * @brief Start playing @p clip, blending out of whatever is playing now.
     *
     * The blend is between two *moving* poses: the outgoing clip keeps advancing
     * while it fades, so a run that fades into a walk does not freeze one foot.
     * It runs down in simulation seconds and is not scaled by `speed`, because
     * "blend over 0.2 seconds" is the contract a caller can predict.
     *
     * Two slots hold two clips. Calling this again while a fade is in flight
     * drops the clip already on its way out and blends from the one that was
     * being faded to - which is the one still on screen.
     *
     * @param animator Animator to retarget, in place.
     * @param clip Clip to play. The same clip already playing is left alone,
     *             rather than restarted from zero for no visible reason.
     * @param seconds Blend length. Zero or less is a cut, as is having no clip
     *                to blend out of.
     * @param looping Whether @p clip wraps at its end. Taken here rather than
     *                assigned by the caller around this call, because the clip
     *                on its way out keeps the answer it was playing under and
     *                only this can capture that before it is overwritten.
     */
    static void crossFadeTo(Animator& animator, AnimationClipHandle clip, float seconds,
                            bool looping);
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::Animator)
    VKM_F(skeleton),
    VKM_F(clip),
    VKM_F(speed),
    VKM_F(playOnStart),
    VKM_F(looping)
VKM_REFLECT_END()
