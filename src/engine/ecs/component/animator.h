#pragma once

#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/skeleton_asset.h"

namespace Vkm::Engine {

/**
 * @brief What drives a rig: the skeleton, the clip playing on it, and where in
 *        that clip playback stands.
 *
 * There is one Animator per character, not one per mesh. Import spawns a
 * sub-entity per mesh, so a rigged character arrives as body plus clothes plus
 * hair; a pose on the mesh would mean three clocks drifting apart, or two of
 * the three silently frozen in bind pose. The Animator sits on the rig, and
 * SkeletalAnimationSystem publishes the pose it produces for the whole subtree
 * beneath it.
 *
 * The rig it poses is not a `SkinnedMesh` component either: a mesh is skinned
 * exactly when its MeshAsset carries skin weights, which the asset already
 * knows, and the rig driving it is the nearest Animator at or above it in the
 * Hierarchy - the structure import produces anyway. That relationship needs no
 * EntityId in any serialized row, so it survives prefabs, undo and scene load
 * without a remap.
 *
 * The first six fields are persisted. Blend state deliberately is not, and will
 * not be: a crossfade is two clips and a countdown, and freezing that shape into
 * a scene row would outlive the blend system that wrote it, in a project with no
 * migration path. A saved scene reloads mid-blend as the clip it was blending
 * to, already there - which is where it was going, one fade early.
 */
struct Animator {
    SkeletonHandle      skeleton;  ///< The rig posed; nothing is posed without it.
    AnimationClipHandle clip;      ///< Clip playing on it; empty holds the bind pose.

    float time    = 0.0f;   ///< Playback head, seconds into the clip.
    float speed   = 1.0f;   ///< Playback multiplier applied to the simulation delta.
    bool  playing = true;
    bool  looping = true;

    // Transient: runtime state, never serialized. A layer or blend-tree system
    // replaces these four fields without touching the six above.
    AnimationClipHandle fadeFrom;            ///< Clip being left; empty when nothing is fading.
    float               fadeTime      = 0.0f;   ///< Its own playback head - it keeps playing while it fades.
    float               fadeRemaining = 0.0f;   ///< Simulation seconds of blend still to run.
    float               fadeDuration  = 0.0f;   ///< What it started at, which is what the weight is measured against.

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
     */
    static void crossFadeTo(Animator& animator, AnimationClipHandle clip, float seconds);
};

} // namespace Vkm::Engine
