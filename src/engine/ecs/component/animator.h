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
 * Every field here is persisted. Blend state deliberately is not, and will not
 * be: a crossfade is two clips and a countdown, and freezing that shape into a
 * scene row would outlive the blend system that wrote it, in a project with no
 * migration path.
 */
struct Animator {
    SkeletonHandle      skeleton;  ///< The rig posed; nothing is posed without it.
    AnimationClipHandle clip;      ///< Clip playing on it; empty holds the bind pose.

    float time    = 0.0f;   ///< Playback head, seconds into the clip.
    float speed   = 1.0f;   ///< Playback multiplier applied to the simulation delta.
    bool  playing = true;
    bool  looping = true;
};

} // namespace Vkm::Engine
