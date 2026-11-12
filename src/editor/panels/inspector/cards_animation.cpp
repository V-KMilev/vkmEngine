#include "panels/inspector/cards.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "ecs/component/animation/animation.h"
#include "ecs/component/animation/animator.h"
#include "ecs/component/animation/bone_socket.h"
#include "ecs/component/scene/hierarchy.h"
#include "ecs/component/scene/transform.h"
#include "undo/component_edit.h"
#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "system/animation/animation_system.h"
#include "ui/editor_widgets.h"
#include "editor_state.h"
#include "ui/clip_transport.h"
namespace Vkm::Engine {

namespace {

/**
 * @brief A combo of @p skeleton's joints, indented by depth, filling the row's control column.
 *
 * A null skeleton shows the stored name disabled, so an unloaded rig's socket shows what it awaits.
 *
 * @param skeleton The rig the Animator above the socket resolved, or null.
 * @param bone     The joint's name, written on a pick.
 * @return Whether a pick changed it.
 */
bool pickBone(const SkeletonAsset* skeleton, std::string& bone) {
    ImGui::BeginDisabled(skeleton == nullptr);
    const int picked = searchCombo("##v", bone.empty() ? "(none)" : bone.c_str(), [&] {
        std::vector<ComboRow> rows;
        rows.push_back({"(none)", bone.empty()});
        // BeginDisabled does not close an open list, and the rig can vanish under it.
        if (!skeleton) {
            ImGui::CloseCurrentPopup();
            return rows;
        }
        // Indented by depth: a parent comes before its children, so one pass.
        std::vector<int> depth(skeleton->bones.size(), 0);
        for (size_t i = 0; i < skeleton->bones.size(); ++i) {
            const Bone& joint = skeleton->bones[i];
            depth[i] = joint.parent < 0 ? 0 : depth[static_cast<size_t>(joint.parent)] + 1;
            rows.push_back({joint.name, joint.name == bone, false, depth[i]});
        }
        return rows;
    });
    ImGui::EndDisabled();
    if (picked < 0) return false;
    bone = picked == 0 ? std::string() : skeleton->bones[static_cast<size_t>(picked) - 1].name;
    return true;
}

/// A playhead's slider over a clip's length; true when it was moved.
bool timeSlider(const char* id, float& time, float duration) {
    char format[32];
    std::snprintf(format, sizeof(format), "%%.2f / %.2f s", duration);
    ImGui::SetNextItemWidth(-1.0f);
    return sliderFloat(id, &time, 0.0f, duration, format);
}

} // namespace

CardInfo cardInfo(Tag<Animation>) {
    const char* hint = "Plays a clip on its transform: a door, a bob";
    return {"Animation", AddSection::Animation, hint, EditorIcon::Anim};
}

void drawCard(Rows<Animation>& c, CardContext& card) {
    // Animation is not reflected: its tracks are the Animation panel's, and the card edits what is
    // left by hand. The playhead shows by posing, recorded as poseStep says, after the card's step.
    Animation& anim = c.value();
    Transform* transform = card.scene.tryGet<Transform>(card.id);
    const Transform beforePose = transform ? *transform : Transform{};
    const auto pose = [&] { if (transform) AnimationSystem::poseAt(anim, anim.time, *transform); };

    // Loop and Speed are saved, so they push an edit; Play, Stop and scrubbing only pose.
    const ClipTransport transport = clipTransport("anim", anim, 0.0f, -1.0f);
    if (transport.rewound) pose();
    if (transport.looped || transport.sped) c.edited();
    const char* lengthTip = "How long it runs; 0 is to its last key";
    if (propDrag("Length", &anim.length, 0.02f, 0.0f, 100000.0f, "%.2f s", lengthTip)) c.edited();
    const char* playOnStartTip = "Starts by itself on Play, never while a scene is only open";
    if (propCheckbox("Play On Start", &anim.playOnStart, playOnStartTip)) c.edited();

    // Only AnimationSystem::fixedUpdate advances playback, so with the clock paused it holds.
    if (anim.playing && card.simDelta <= 0.0f) {
        cardNote(Note::Info, "Held at %.2fs: it advances while the world runs.", anim.time);
    }
    const float duration = Animation::computeDuration(anim);
    if (duration > 0.0f && timeSlider("##time", anim.time, duration)) {
        anim.playing = false;
        pose();
    }
    const auto keys = [](const char* label, size_t count, float seconds) {
        ImGui::TextDisabled("%s: %zu key%s, %.2fs", label, count, count == 1 ? "" : "s", seconds);
    };
    keys("Position", anim.positionTrack.keyframeCount(), anim.positionTrack.getDuration());
    keys("Rotation", anim.rotationTrack.keyframeCount(), anim.rotationTrack.getDuration());
    keys("Scale", anim.scaleTrack.keyframeCount(), anim.scaleTrack.getDuration());
    cardNote(Note::Info, "Its keyframes are the Animation panel's.");

    if (!transform) return;
    const EntityId id = card.id;
    card.after = [&card, id, beforePose] {
        const Transform* now = card.scene.tryGet<Transform>(id);
        if (!now) return;
        const char* label = "Scrub Animation";
        auto posed = poseStep(card.scene, card.resources, id, beforePose, *now, card.playing, label);
        if (posed) card.state.pushStep(std::move(posed));
    };
}

CardInfo cardInfo(Tag<Animator>) {
    return {"Animator", AddSection::Animation, "Poses a rigged mesh with clips", EditorIcon::Skeleton};
}

void drawCard(Rows<Animator>& c, CardContext& card) {
    Animator& animator = c.value();
    c.row<&Animator::skeleton>();
    c.row<&Animator::clip>();
    const SkeletonAsset* rig = card.resources.tryGet(animator.skeleton);
    const AnimationClipAsset* clip = card.resources.tryGet(animator.clip);
    if (rig) ImGui::TextDisabled("%zu bones", rig->bones.size());
    // A clip is cooked against one rig's bone order; SkeletalAnimationSystem::resolveClip refuses
    // a mismatch and holds the bind pose.
    if (rig && clip && clip->skeleton != rig->name()) {
        const char* owner = clip->skeleton.c_str();
        cardNote(Note::Danger, "The clip is for rig '%s': the bind pose holds until they match.", owner);
    }

    // Loop and Speed are authored; play, stop and scrubbing are not, and scrubbing works paused:
    // SkeletalAnimationSystem::update still poses the rig.
    const ClipTransport transport = clipTransport("rig", animator, -10.0f, -1.0f);
    if (transport.looped) c.edited<&Animator::looping>();
    if (transport.sped) c.edited<&Animator::speed>();
    c.row<&Animator::playOnStart>();

    // Only SkeletalAnimationSystem::fixedUpdate moves the head, so in Edit mode it holds.
    if (animator.playing && card.simDelta <= 0.0f) {
        const double time = static_cast<double>(animator.time);
        cardNote(Note::Info, "Held at %.2fs: it advances while the world runs.", time);
    }
    if (clip && clip->duration > 0.0f) {
        timeSlider("##time", animator.time, clip->duration);
    } else if (animator.skeleton) {
        cardNote(Note::Info, "No clip: holding the bind pose.");
    }
    if (clip && !clip->markers.empty()) {
        for (const ClipMarker& marker : clip->markers) {
            ImGui::TextDisabled("Marker %s at %.2fs", marker.name.c_str(), static_cast<double>(marker.time));
        }
        cardNote(Note::Info, "Markers fire as AnimationEvent; they are the clip's recipe's.");
    }
}

CardInfo cardInfo(Tag<BoneSocket>) {
    const char* hint = "Follows a bone of a rig: a sword in a hand";
    return {"Bone Socket", AddSection::Animation, hint, EditorIcon::Socket};
}

void drawCard(Rows<BoneSocket>& c, CardContext& card) {
    BoneSocket& socket = c.value();
    // The rig must be the parent: BoneSocketSystem writes the local Transform against it, so one
    // hung deeper lands plausibly wrong.
    const Hierarchy* node = card.scene.tryGet<Hierarchy>(card.id);
    const Animator* animator = card.scene.tryGet<Animator>(node ? node->parent : EntityId{});
    const SkeletonAsset* skeleton = animator ? card.resources.tryGet(animator->skeleton) : nullptr;
    if (!animator) {
        cardNote(Note::Danger, "The parent is not a rig: drag this onto the entity with the Animator.");
    } else if (!skeleton) {
        cardNote(Note::Danger, "The rig above names no skeleton: pick one on its Animator.");
    }

    const auto bones = [&](std::string& bone, const FieldInfo&) { return pickBone(skeleton, bone); };
    c.rowWith<&BoneSocket::bone>(bones);
    // An unknown bone name fails silently: the socket stays put.
    if (skeleton && !socket.bone.empty() && skeleton->indexOf(socket.bone) < 0) {
        const char* rig = skeleton->name().c_str();
        cardNote(Note::Danger, "Rig '%s' has no bone '%s': it stays put.", rig, socket.bone.c_str());
    }

    // Authored here: the Transform is the socket's output, rewritten every frame, paused too.
    sectionLabel("Offset");
    Transform& offset = c.custom<&BoneSocket::offset>();
    const char* tip = c.tipOf<&BoneSocket::offset>();
    bool moved = drawVec3Control("Position", tip, &offset.position.x, 0.0f, 0.01f);
    card.memory.socketEuler.sync(card.id, offset.rotation);
    if (drawVec3Control("Rotation", tip, card.memory.socketEuler.degrees(), 0.0f, 0.5f)) {
        offset.rotation = card.memory.socketEuler.toQuat();
        moved           = true;
    }
    moved |= drawVec3Control("Scale", tip, &offset.scale.x, 1.0f, 0.01f);
    if (moved) c.edited();
}

} // namespace Vkm::Engine
