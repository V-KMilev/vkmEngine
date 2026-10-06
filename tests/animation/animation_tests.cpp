#include "support.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "core/math/easing.h"
#include "ecs/component/animation/animation.h"
#include "ecs/component/animation/animation_track.h"
#include "ecs/component/animation/animator.h"
#include "ecs/component/animation/bone_socket.h"
#include "ecs/component/core/hierarchy.h"
#include "ecs/component/core/name.h"
#include "ecs/hierarchy_operations.h"
#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "system/animation/animation_events.h"
#include "system/animation/animation_system.h"
#include "system/animation/bone_socket_system.h"
#include "system/animation/skeletal_animation_system.h"

namespace {

// speed may be negative, so a clip's near end must wrap and stop as the far end does.
// AnimationTrack answers its first key for any time at or before it, so a head left
// below zero would freeze the opening pose with `playing` still true, for good.
void testAClipRunBackwardsWrapsAndStopsLikeOneRunForwards() {
    std::printf("An Animation with a negative speed:\n");

    Scene scene;
    const EntityId id = scene.createEntity();
    scene.add(id, Transform{});

    Animation anim;
    anim.length      = 1.0f;
    anim.playOnStart = true;
    anim.speed       = -1.0f;
    anim.positionTrack.addKeyframe(0.0f, glm::vec3(0.0f));
    anim.positionTrack.addKeyframe(1.0f, glm::vec3(10.0f, 0.0f, 0.0f));
    scene.add(id, std::move(anim));

    TestFrame       frame(scene);
    AnimationSystem animation;
    const float step = frame.clock.getFixedStep();

    // Looping: the head steps back past zero and comes round to the end.
    scene.get<Animation>(id).looping = true;
    scene.get<Animation>(id).time    = step * 0.25f;
    animation.fixedUpdate(frame.ctx);   // starts it
    animation.fixedUpdate(frame.ctx);   // and takes it under zero

    const Animation& wrapped = scene.get<Animation>(id);
    check("a backwards clip wraps to the end rather than running off", wrapped.playing);
    check("  landing inside the clip", wrapped.time > 0.0f && wrapped.time < 1.0f);
    check("  near the end it came round to", wrapped.time > 0.5f);
    check("  and posed there, not frozen on the first key", scene.get<Transform>(id).position.x > 5.0f);

    // Not looping: it stops at the start, mirroring the end.
    scene.get<Animation>(id).looping = false;
    scene.get<Animation>(id).playing = true;
    scene.get<Animation>(id).time    = step * 0.25f;
    for (int i = 0; i < 8; ++i) animation.fixedUpdate(frame.ctx);

    const Animation& stopped = scene.get<Animation>(id);
    check("a backwards clip that does not loop stops", !stopped.playing);
    check("  at its start rather than below it", nearly(stopped.time, 0.0f));
    check("  posed on its first keyframe", nearly(scene.get<Transform>(id).position.x, 0.0f));
}

// Every boundary getValue special-cases is pinned: each returns a plausible pose when wrong.
void testATrackHoldsItsEndsRatherThanFallingOff() {
    std::printf("What a track answers outside the keys it has:\n");

    AnimationTrack<float> track;
    track.addKeyframe(1.0f, 10.0f);
    track.addKeyframe(3.0f, 30.0f);

    check("the last key is reachable", nearly(track.getValue(3.0f), 30.0f));
    check("  and holds past the end", nearly(track.getValue(99.0f), 30.0f));
    check("the first key holds before the start", nearly(track.getValue(0.0f), 10.0f));

    // The first key is at 1.0, not 0, so 0.5 precedes every key; an unguarded
    // upper_bound would return begin() and underflow the previous index (see getValue).
    check(
        "  including between zero and a first key that is not at zero",
        nearly(track.getValue(0.5f), 10.0f)
    );
    check("  and at a negative time", nearly(track.getValue(-5.0f), 10.0f));

    check("the midpoint interpolates", nearly(track.getValue(2.0f), 20.0f));
    check("duration is the last key's time", nearly(track.getDuration(), 3.0f));
}

void testATrackWithOneKeyIsThatKey() {
    std::printf("Tracks with nothing to interpolate between:\n");

    AnimationTrack<float> single;
    single.addKeyframe(2.0f, 7.0f);
    check("one key answers itself before it", nearly(single.getValue(0.0f), 7.0f));
    check("  at it", nearly(single.getValue(2.0f), 7.0f));
    check("  and after it", nearly(single.getValue(100.0f), 7.0f));

    AnimationTrack<float> empty;
    check("an empty track is empty", empty.isEmpty());
    check("  answers a default", nearly(empty.getValue(1.0f), 0.0f));
    check("  and has no duration", nearly(empty.getDuration(), 0.0f));
}

void testKeysAreOrderedByTimeHoweverTheyArrive() {
    std::printf("Keyframes added out of order:\n");

    AnimationTrack<float> track;
    track.addKeyframe(3.0f, 30.0f);
    track.addKeyframe(1.0f, 10.0f);
    track.addKeyframe(2.0f, 20.0f);

    // getValue binary-searches the times, so an unsorted track quietly answers the
    // wrong key.
    check("three keys are held", track.keyframeCount() == 3);
    check("the earliest is first", nearly(track.getValue(1.0f), 10.0f));
    check("  the middle is between them", nearly(track.getValue(2.0f), 20.0f));
    check("  and the latest is last", nearly(track.getValue(3.0f), 30.0f));
    check("interpolation runs the right way round", nearly(track.getValue(1.5f), 15.0f));
}

void testRetimingAKeyPastAnotherReportsWhereItLanded() {
    std::printf("Dragging a keyframe past its neighbour:\n");

    AnimationTrack<float> track;
    track.addKeyframe(0.0f, 0.0f);
    track.addKeyframe(0.5f, 50.0f);
    track.addKeyframe(1.0f, 100.0f);

    // What the timeline does each frame while a dot is held: the track re-sorts on
    // every retime, so the drag's index stops naming its key once two cross.
    const size_t moved = track.setKeyframeTime(1, 1.5f);
    check("the retimed key reports its new index", moved == 2);
    check("  and its value travelled with it", nearly(track.getValues()[moved], 50.0f));
    check("  leaving the neighbour where it was", nearly(track.getValues()[1], 100.0f));

    const size_t again = track.setKeyframeTime(moved, 1.6f);
    check("following the answer keeps hold of the same key", nearly(track.getValues()[again], 50.0f));
    check("  and the one it passed is untouched", nearly(track.getValue(1.0f), 100.0f));
    check(
        "  with the track still in time order",
        track.getTimes()[0] < track.getTimes()[1] && track.getTimes()[1] < track.getTimes()[2]
    );
}

void testTwoKeysAtTheSameTimeDoNotDivideByZero() {
    std::printf("Two keys sharing one time:\n");

    AnimationTrack<float> track;
    track.addKeyframe(1.0f, 10.0f);
    track.addKeyframe(1.0f, 20.0f);
    track.addKeyframe(2.0f, 30.0f);

    // upper_bound returns the first key strictly after the query, so the straddled pair
    // never shares a time and the zero-segment guard is defensive. Pinned: a number,
    // never a NaN.
    const float at = track.getValue(1.0f);
    check("the answer at the shared time is a number", std::isfinite(at));
    check("  and so is one just after it", std::isfinite(track.getValue(1.0001f)));
    check("  and one between it and the next key", std::isfinite(track.getValue(1.5f)));
}

// A curve is saved by name (core/math/easing.h says why not an address). This pins
// the two conversions against each other, for every row.
void testEveryEasingSurvivesBeingWrittenDown() {
    std::printf("What an easing is when it is saved:\n");

    constexpr size_t COUNT = Reflect::EnumNames<Easing>::count;
    int mismatches = 0;
    for (size_t i = 0; i < COUNT; ++i) {
        const Easing easing = static_cast<Easing>(i);
        Easing back = Easing::Count;
        if (!Reflect::enumFromNameChecked(Reflect::enumName(easing), back) || back != easing) {
            std::printf("      row %zu (%s) does not come back as itself\n", i, Reflect::enumName(easing));
            ++mismatches;
        }
    }

    // Distinct names, or two rows collapse on the way back and the loop above still
    // passes for whichever the search reaches first.
    std::vector<std::string> names;
    for (size_t i = 0; i < COUNT; ++i) names.emplace_back(Reflect::enumName(static_cast<Easing>(i)));
    std::sort(names.begin(), names.end());
    const bool unique = std::adjacent_find(names.begin(), names.end()) == names.end();

    std::printf("      %zu curves\n", COUNT);
    check("the table has rows", COUNT > 1);
    check("every curve's name reads back as the same curve", mismatches == 0);
    check("and no two curves answer to one name", unique);

    const bool linearFirst = static_cast<size_t>(Easing::Linear) == 0;
    check(
        "linear is the row everything falls back to",
        linearFirst && std::strcmp(Reflect::enumName(Easing::Linear), "linear") == 0
    );
    Easing unknown = Easing::Linear;
    check(
        "a name this build does not carry leaves the curve as it was",
        !Reflect::enumFromNameChecked("easeInOutNothing", unknown) && unknown == Easing::Linear
    );

    // Out of range is reachable only by a cast; it must not read past either table.
    check(
        "a value past the table is saved and evaluated as linear",
        std::strcmp(Reflect::enumName(Easing::Count), "linear") == 0
            && easingFunction(Easing::Count) == easingFunction(Easing::Linear)
    );
}

void testEasingChangesTheShapeAndNotTheEnds() {
    std::printf("A track with a curve on it:\n");

    AnimationTrack<float> track;
    track.addKeyframe(0.0f, 0.0f);
    track.addKeyframe(1.0f, 100.0f);
    track.setEasing(Easing::EaseInOutSine);

    // Whatever the curve, the keys are what the author placed.
    check("the start key is untouched by easing", nearly(track.getValue(0.0f), 0.0f));
    check("  and the end key is too", nearly(track.getValue(1.0f), 100.0f));

    const float mid = track.getValue(0.5f);
    check("the middle is still inside the range", mid > 0.0f && mid < 100.0f);

    AnimationTrack<float> linear;
    linear.addKeyframe(0.0f, 0.0f);
    linear.addKeyframe(1.0f, 100.0f);
    check(
        "and an eased quarter differs from a linear one",
        !nearly(track.getValue(0.25f), linear.getValue(0.25f))
    );
}

constexpr bool LOOPING = true;
constexpr bool ONE_SHOT = false;
constexpr float BACKWARDS = -1.0f;

// The marker path, through the real SkeletalAnimationSystem over a real Scene. A
// marker claim is a count of events on the bus over frames, so only counting them
// settles it; re-deriving crossesMarker's arithmetic would pass whatever it became.
//
// The clock single-steps through requestStep(), the editor's path, so each frame is
// one fixed step and expected counts are exact.
struct MarkerWorld {
    Scene           scene;
    TestFrame       frame{scene};
    SkeletalAnimationSystem animation;
    EntityId        rig;
    std::vector<std::string> heard;   ///< Marker names, in the order they arrived.
    std::vector<size_t>      perFrame;  ///< How many arrived on each frame.

    MarkerWorld(
        float duration,
        std::vector<ClipMarker> markers,
        bool looping = true,
        float speed = 1.0f,
        int tickRate = 60
    ) {
        // One bone is a rig: the marker path never looks at the pose.
        SkeletonAsset skeleton;
        skeleton.bones.push_back(Bone{"root", -1});
        skeleton.inverseBind.push_back(glm::mat4(1.0f));
        skeleton.bindPose.push_back(Transform{});
        const SkeletonHandle rigAsset =
            frame.resources.add(std::move(skeleton), "rig");

        AnimationClipAsset clip;
        clip.skeleton = "rig";          // resolveClip matches on the rig's name
        clip.duration = duration;
        clip.bones.resize(1);           // and on its bone count
        clip.markers = std::move(markers);
        const AnimationClipHandle clipAsset =
            frame.resources.add(std::move(clip), "clip");

        rig = scene.createEntity();
        Animator animator;
        animator.skeleton = rigAsset;
        animator.clip     = clipAsset;
        animator.looping  = looping;
        animator.speed    = speed;
        animator.playing  = true;
        animator.started  = true;       // playOnStart is the runtime's business
        scene.add(rig, std::move(animator));

        frame.events.subscribe<AnimationEvent>([this](const AnimationEvent& e) {
            heard.push_back(e.marker);
        });

        frame.clock.setTickRate(tickRate);
        frame.clock.setPaused(true);
    }

    Animator& animator() { return scene.get<Animator>(rig); }

    /// Run @p frames frames, each exactly one fixed step.
    void tick(int frames) {
        for (int i = 0; i < frames; ++i) {
            const size_t before = heard.size();
            frame.clock.requestStep(1);
            frame.clock.beginFrame();
            while (frame.clock.consumeFixedStep()) animation.fixedUpdate(frame.ctx);
            frame.events.flush();
            perFrame.push_back(heard.size() - before);
        }
    }

    /// Run @p frames frames with the clock paused and no step requested.
    void tickPaused(int frames) {
        for (int i = 0; i < frames; ++i) {
            frame.clock.beginFrame();
            while (frame.clock.consumeFixedStep()) animation.fixedUpdate(frame.ctx);
            animation.update(frame.ctx);
            frame.events.flush();
        }
    }

    size_t countOf(const char* name) const {
        return static_cast<size_t>(std::count(heard.begin(), heard.end(), name));
    }
};

void testALoopingClipAnnouncesEachMarkerOncePerLap() {
    std::printf("A looping clip's markers over many laps:\n");

    // One second at 60 Hz: a lap is 60 frames, 600 frames ten laps. Markers sit off
    // frame boundaries, so none lands on the seam where a lap wraps.
    MarkerWorld world(1.0f, {{"a", 0.25f}, {"b", 0.5f}, {"c", 0.75f}});
    world.tick(600);

    check(
        "each marker announced once per lap, ten laps in",
        world.countOf("a") == 10 && world.countOf("b") == 10 && world.countOf("c") == 10
    );
    check("and nothing else was announced", world.heard.size() == 30);

    bool twiceInARow = false;
    for (size_t i = 1; i < world.heard.size(); ++i) {
        if (world.heard[i] == world.heard[i - 1]) twiceInARow = true;
    }
    check("no marker announced twice in a row", !twiceInARow);

    bool oneFramePerMarker = true;
    for (size_t n : world.perFrame) {
        if (n > 1) oneFramePerMarker = false;
    }
    check("and no frame announced more than the one marker it crossed", oneFramePerMarker);
}

void testANegativeSpeedComesBackOverTheSameMarkers() {
    std::printf("A clip played backwards:\n");

    MarkerWorld world(1.0f, {{"a", 0.25f}, {"b", 0.5f}, {"c", 0.75f}}, LOOPING, BACKWARDS);
    world.tick(600);

    check(
        "each marker still announced once per lap",
        world.countOf("a") == 10 && world.countOf("b") == 10 && world.countOf("c") == 10
    );
    check(
        "and they arrived in the reverse order",
        world.heard.size() >= 3 && world.heard[0] == "c" && world.heard[1] == "b" && world.heard[2] == "a"
    );
}

void testANonLoopingClipStopsAtItsEnd() {
    std::printf("A clip that does not loop:\n");

    // The marker sits on the last reachable instant, whose frame depends on float
    // accumulation over sixty steps, so this runs until the head clamps.
    MarkerWorld world(1.0f, {{"end", 1.0f}}, ONE_SHOT);
    int frames = 0;
    while (world.animator().playing && frames < 300) {
        world.tick(1);
        ++frames;
    }
    check("the head reached the end and stopped", !world.animator().playing);
    check(
        "and the marker is announced on the frame it landed there",
        world.countOf("end") == 1 && !world.perFrame.empty() && world.perFrame.back() == 1
    );

    world.tick(300);
    check("a clamped head announces nothing more", world.countOf("end") == 1);
}

void testAPausedClockAnnouncesNothing() {
    std::printf("A paused animation:\n");

    MarkerWorld world(1.0f, {{"a", 0.25f}});
    world.tickPaused(300);
    check("three hundred paused frames announce nothing", world.heard.empty());
    check("and move nothing", nearly(world.animator().time, 0.0f));

    world.tick(20);
    check("the frames that resume announce the marker one lap reaches", world.countOf("a") == 1);
}

void testAScrubbedHeadAnnouncesNothingAndAStoppedOneNeither() {
    std::printf("A head moved by hand, and one not moving at all:\n");

    MarkerWorld scrubbed(1.0f, {{"a", 0.25f}});
    scrubbed.animator().time = 0.9f;   // dragged past the marker in the editor
    scrubbed.tick(1);
    check("scrubbing past a marker announces nothing", scrubbed.heard.empty());

    MarkerWorld stopped(1.0f, {{"a", 0.25f}});
    stopped.animator().playing = false;
    stopped.tick(300);
    check("and a stopped animator announces nothing over three hundred frames", stopped.heard.empty());
}

void testACrossfadeAnnouncesOnlyTheIncomingClip() {
    std::printf("A crossfade between two marked clips:\n");

    MarkerWorld world(1.0f, {{"incoming", 0.5f}});

    // A second clip on the same rig, marked where the outgoing head crosses mid-fade.
    AnimationClipAsset outgoing;
    outgoing.skeleton = "rig";
    outgoing.duration = 1.0f;
    outgoing.bones.resize(1);
    outgoing.markers.push_back(ClipMarker{"outgoing", 0.1f});
    const AnimationClipHandle outgoingAsset =
        world.frame.resources.add(std::move(outgoing), "outgoing");

    Animator& animator = world.animator();
    animator.clip = outgoingAsset;
    animator.time = 0.0f;
    // Fade over half a second: the outgoing head reaches its marker six frames in.
    const AnimationClipHandle marked = world.frame.resources.findByName<AnimationClipAsset>("clip");
    Animator::crossFadeTo(animator, marked, 0.5f, LOOPING);
    check("the fade is in flight", world.animator().fadeRemaining > 0.0f);

    world.tick(12);
    check("the outgoing clip's marker is not announced", world.countOf("outgoing") == 0);
    check("and the fade was still running while it would have been", world.animator().fadeRemaining > 0.0f);

    world.tick(48);
    check("the incoming clip's marker is announced", world.countOf("incoming") == 1);
}

void testAOneShotFadingOutClampsRatherThanWrapping() {
    std::printf("A one-shot fading out under a looping clip:\n");

    // The outgoing clip is 0.2 s and does not loop; the fade is 0.5 s, so the head hits
    // the clip's end mid-blend. It must stop there, not start again.
    MarkerWorld world(1.0f, {});
    AnimationClipAsset shortClip;
    shortClip.skeleton = "rig";
    shortClip.duration = 0.2f;
    shortClip.bones.resize(1);
    const AnimationClipHandle shortAsset =
        world.frame.resources.add(std::move(shortClip), "oneshot");

    Animator& animator = world.animator();
    animator.clip    = shortAsset;
    animator.time    = 0.0f;
    animator.looping = false;
    const AnimationClipHandle incoming = world.frame.resources.findByName<AnimationClipAsset>("clip");
    Animator::crossFadeTo(animator, incoming, 0.5f, LOOPING);
    check("the incoming clip loops", world.animator().looping);
    check("and the outgoing one is remembered as not looping", !world.animator().fadeLooping);

    world.tick(24);   // 0.4s: twice the outgoing clip's length
    check("the fade is still running", world.animator().fadeRemaining > 0.0f);
    check(
        "and the outgoing head stopped at the end of its clip rather than wrapping",
        nearly(world.animator().fadeTime, 0.2f)
    );
}

// A finished one-shot still names its clip, so "already playing it" asked of the name
// alone would leave a second flinch frozen on the first one's last frame, for good.
void testAFinishedOneShotAskedForAgainPlaysAgain() {
    std::printf("A one-shot asked for again after it ended:\n");

    MarkerWorld world(1.0f, {{"early", 0.1f}}, ONE_SHOT);
    world.tick(90);   // a second and a half: past the end of a one-second clip
    check("it ran out", !world.animator().playing && world.countOf("early") == 1);

    const AnimationClipHandle same = world.animator().clip;
    Animator::crossFadeTo(world.animator(), same, 0.2f, ONE_SHOT);
    check("asked for again, it plays", world.animator().playing);
    check("  from its start", nearly(world.animator().time, 0.0f));

    world.tick(30);
    check("  and crosses its marker a second time", world.countOf("early") == 2);

    // Still playing now, so the rule against restarting it stands.
    const float head = world.animator().time;
    Animator::crossFadeTo(world.animator(), same, 0.2f, ONE_SHOT);
    check("asked for again while it plays, it carries on", nearly(world.animator().time, head));
}

// Starting a clip puts the head at 0, which for a negative speed is the end it runs
// toward: a backwards one-shot would stop on its first tick. Both kinds of head start
// from the far end.
void testAOneShotPlayedBackwardsStartsFromItsEnd() {
    std::printf("A one-shot started with a negative speed:\n");

    MarkerWorld world(1.0f, {{"late", 0.9f}}, ONE_SHOT, BACKWARDS);
    world.tick(30);
    check("a rig's clip plays from its end", world.animator().playing);
    check("  half way back after half its length", nearly(world.animator().time, 0.5f));
    check("  having crossed the marker near its end", world.countOf("late") == 1);
    world.tick(60);
    check("  and stops at its start", !world.animator().playing && nearly(world.animator().time, 0.0f));

    Scene scene;
    const EntityId id = scene.createEntity();
    scene.add(id, Transform{});
    Animation anim;
    anim.length      = 1.0f;
    anim.playOnStart = true;
    anim.looping     = false;
    anim.speed       = -1.0f;
    anim.positionTrack.addKeyframe(0.0f, glm::vec3(0.0f));
    anim.positionTrack.addKeyframe(1.0f, glm::vec3(10.0f, 0.0f, 0.0f));
    scene.add(id, std::move(anim));

    TestFrame       frame(scene);
    AnimationSystem animation;
    animation.fixedUpdate(frame.ctx);
    check("a keyframe animation started backwards plays", scene.get<Animation>(id).playing);
    check("  posed near its last key", scene.get<Transform>(id).position.x > 9.0f);
}

void testTwoRigsOnOneClipAnnounceOnTheirOwnPhase() {
    std::printf("Two characters half a lap apart on one clip:\n");

    MarkerWorld world(1.0f, {{"a", 0.25f}});

    const EntityId second = world.scene.createEntity();
    Animator other = world.animator();   // same handles, its own head
    other.time = 0.5f;
    world.scene.add(second, std::move(other));

    std::vector<EntityId> from;
    world.frame.events.subscribe<AnimationEvent>([&](const AnimationEvent& e) { from.push_back(e.entity); });

    world.tick(60);
    check("each rig announced once over the lap", world.countOf("a") == 2);
    check("and they were two different rigs", from.size() == 2 && from[0] != from[1]);
}

// Markers reach gameplay, which the simulation replays, so rigs crossing one on the
// same tick announce in slot order, not Animator storage order.
void testRigsAnnounceInSlotOrder() {
    std::printf("Two rigs crossing a marker on one tick:\n");

    MarkerWorld world(1.0f, {{"a", 0.25f}});
    const EntityId later = world.scene.createEntity();
    Animator copy = world.animator();
    world.scene.add(later, Animator(copy));
    world.scene.remove<Animator>(world.rig);
    world.scene.add(world.rig, std::move(copy));   // now stored after the later slot

    std::vector<EntityId> from;
    world.frame.events.subscribe<AnimationEvent>([&](const AnimationEvent& e) { from.push_back(e.entity); });

    world.tick(20);
    check(
        "both announce, the lower slot first",
        from.size() == 2 && from[0] == world.rig && from[1] == later
    );
}

// A rig with one bone lifted two metres and a socket off it: enough to tell "placed at
// the bone" from "left where it was", the one thing BoneSocketSystem does.
void testASocketRidesItsBone() {
    std::printf("Where a bone socket ends up:\n");

    Scene     scene;
    TestFrame frame{scene};
    SkeletalAnimationSystem animation;
    BoneSocketSystem        sockets;

    SkeletonAsset skeleton;
    skeleton.bones.push_back(Bone{"hand", -1});
    skeleton.inverseBind.push_back(glm::mat4(1.0f));
    Transform bind;
    bind.position = {0.0f, 2.0f, 0.0f};
    skeleton.bindPose.push_back(bind);
    const SkeletonHandle rigAsset = frame.resources.add(std::move(skeleton), "rig");

    const EntityId rig = scene.createEntity();
    scene.add(rig, Transform{});
    Animator animator;
    animator.skeleton = rigAsset;   // no clip: the bind pose is the pose
    scene.add(rig, std::move(animator));

    const EntityId socket = scene.createEntity();
    scene.add(socket, makeName("Weapon"));
    scene.add(socket, Transform{});
    BoneSocket attach;
    attach.bone = "hand";
    attach.offset.position = {0.0f, 0.0f, 1.0f};
    scene.add(socket, std::move(attach));
    HierarchyOperations::setParent(scene, socket, rig);

    // The socket runs in the Transform stage, reading the tick's pose. Before any tick
    // there is no pose for this rig - a moment, not a fault - and it holds its place.
    sockets.update(frame.ctx);
    check(
        "with no pose yet, the socket is left where it was",
        scene.get<Transform>(socket).position == glm::vec3(0.0f)
    );

    frame.clock.setPaused(true);
    frame.clock.beginFrame();
    animation.update(frame.ctx);    // paused composes the pose at a zero step
    sockets.update(frame.ctx);

    // Bone at y=2, offset a metre along its local +Z. The socket's LOCAL transform is
    // written: HierarchySystem resolves parentWorld * local later, with the rig as parent.
    const glm::vec3 placed = scene.get<Transform>(socket).position;
    check(
        "once the rig is posed, the socket sits on the bone",
        nearly(placed.y, 2.0f) && nearly(placed.z, 1.0f) && nearly(placed.x, 0.0f)
    );

    // A name the rig lacks places nothing rather than picking a bone.
    scene.get<BoneSocket>(socket).bone = "nose";
    scene.get<Transform>(socket).position = glm::vec3(0.0f);
    sockets.update(frame.ctx);
    check(
        "  and a bone the rig does not have places nothing",
        scene.get<Transform>(socket).position == glm::vec3(0.0f)
    );
}

// In Edit mode nothing steps, so the editor poses a scrubbed playhead through the
// system's own sampling function; it writes what a step would: keyed tracks only.
void testAPoseAtThePlayheadWritesOnlyKeyedChannels() {
    std::printf("Posing an Animation at its playhead:\n");

    Animation anim;
    anim.positionTrack.addKeyframe(0.0f, glm::vec3(0.0f));
    anim.positionTrack.addKeyframe(2.0f, glm::vec3(4.0f, 0.0f, 0.0f));
    anim.time = 1.0f;

    Transform transform;
    transform.scale = glm::vec3(3.0f);
    AnimationSystem::applyAnimation(anim, transform);
    check("a keyed channel takes the track's value at the playhead", nearly(transform.position.x, 2.0f));
    check(
        "  and an empty track leaves its channel as authored",
        nearly(transform.scale.x, 3.0f) && nearly(transform.rotation.w, 1.0f)
    );

    anim.time = 0.0f;
    AnimationSystem::applyAnimation(anim, transform);
    check("a rewound playhead poses the first key", nearly(transform.position.x, 0.0f));
}

// What a scene does on Play: starting, looping, running out, running at a speed.
void testWhatPlayingAnAnimationDoes() {
    std::printf("An Animation on a ticking scene:\n");

    Scene scene;
    const EntityId id = scene.createEntity();
    scene.add(id, Transform{});

    Animation anim;
    anim.length      = 1.0f;
    anim.playOnStart = false;
    anim.positionTrack.addKeyframe(0.0f, glm::vec3(0.0f));
    anim.positionTrack.addKeyframe(1.0f, glm::vec3(10.0f, 0.0f, 0.0f));
    scene.add(id, std::move(anim));

    TestFrame       frame(scene);
    AnimationSystem animation;
    const float step = frame.clock.getFixedStep();

    animation.fixedUpdate(frame.ctx);
    check(
        "an animation that was not told to play does not",
        nearly(scene.get<Animation>(id).time, 0.0f) && nearly(scene.get<Transform>(id).position.x, 0.0f)
    );

    // playOnStart becomes the runtime flag once, so a scene animates on Play and sits
    // still while open.
    scene.get<Animation>(id).playOnStart = true;
    animation.fixedUpdate(frame.ctx);
    check("playOnStart starts it", scene.get<Animation>(id).playing);
    check("  and it advances by one fixed step", nearly(scene.get<Animation>(id).time, step));
    check("  posing the transform as it goes", scene.get<Transform>(id).position.x > 0.0f);

    // Speed multiplies the step: half speed, half the distance.
    scene.get<Animation>(id).time  = 0.0f;
    scene.get<Animation>(id).speed = 0.5f;
    animation.fixedUpdate(frame.ctx);
    check("speed scales how far a step carries it", nearly(scene.get<Animation>(id).time, step * 0.5f));

    // Running off the end of a non-looping clip stops it at the end, not past it, where
    // it would read the last key forever and never say it finished.
    scene.get<Animation>(id).speed   = 1.0f;
    scene.get<Animation>(id).looping = false;
    scene.get<Animation>(id).time    = 0.99f;
    for (int i = 0; i < 8; ++i) animation.fixedUpdate(frame.ctx);
    check(
        "a clip that does not loop stops at its end",
        !scene.get<Animation>(id).playing && nearly(scene.get<Animation>(id).time, 1.0f)
    );
    check("  posed on its last keyframe", nearly(scene.get<Transform>(id).position.x, 10.0f));

    // A loop wraps keeping the overshoot; snapping to zero would lose a step per cycle.
    scene.get<Animation>(id).looping = true;
    scene.get<Animation>(id).playing = true;
    scene.get<Animation>(id).time    = 1.0f - step * 0.25f;
    animation.fixedUpdate(frame.ctx);
    check(
        "a looping clip wraps round",
        scene.get<Animation>(id).playing && scene.get<Animation>(id).time < 0.5f
    );
    check("  carrying its overshoot with it", nearly(scene.get<Animation>(id).time, step * 0.75f));
}

// A clip cannot know where the player aims, how deep a crouch goes or where a hit
// landed; BoneAdjust adds that on top. Its frame - model space, about the bone's own
// origin - only shows once the parent is turned: applied in the parent's frame it
// agrees on an unturned rig and nowhere else.
void testAnAdjustBendsABoneInModelSpace() {
    std::printf("A BoneAdjust on a posed rig:\n");

    Scene     scene;
    TestFrame frame{scene};
    SkeletalAnimationSystem animation;

    // Hips at the origin, a spine one unit up, a head one above that.
    SkeletonAsset skeleton;
    skeleton.bones = {Bone{"hips", -1}, Bone{"spine", 0}, Bone{"head", 1}};
    skeleton.inverseBind.assign(3, glm::mat4(1.0f));
    Transform up;
    up.position = {0.0f, 1.0f, 0.0f};
    skeleton.bindPose = {Transform{}, up, up};
    const SkeletonHandle rigAsset = frame.resources.add(std::move(skeleton), "rig");

    const EntityId rig = scene.createEntity();
    scene.add(rig, Transform{});
    Animator animator;
    animator.skeleton = rigAsset;   // no clip: the bind pose is the pose
    scene.add(rig, std::move(animator));

    frame.clock.setPaused(true);
    frame.clock.beginFrame();

    const auto origin = [&](uint32_t bone) {
        animation.update(frame.ctx);    // paused composes the pose at a zero step
        const PoseSlice* slice = frame.ctx.poses->sliceOf(rig);
        return glm::vec3(frame.ctx.poses->global()[slice->first + bone][3]);
    };
    const auto at = [](const glm::vec3& p, float x, float y, float z) {
        return nearly(p.x, x) && nearly(p.y, y) && nearly(p.z, z);
    };

    check("untouched, the head stands two units up", at(origin(2), 0.0f, 2.0f, 0.0f));

    // A quarter turn about model +X on the spine: +Y goes to +Z, so the head swings
    // forward and the spine's origin stays put.
    BoneAdjust bend;
    bend.bone     = 1;
    bend.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    scene.get<Animator>(rig).adjust = {bend};
    check(
        "a bend on the spine swings the head about the spine's origin",
        at(origin(2), 0.0f, 1.0f, 1.0f) && at(origin(1), 0.0f, 1.0f, 0.0f)
    );

    // The hips yawed a quarter turn first, so the spine's parent X runs along model -Z.
    // The same bend must still take the head to model +Z.
    BoneAdjust yaw;
    yaw.bone     = 0;
    yaw.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    scene.get<Animator>(rig).adjust = {yaw, bend};
    check("  and does so in model space, whatever the parent is doing", at(origin(2), 0.0f, 1.0f, 1.0f));

    // An offset is model space too: +X moves the spine along model +X, not the
    // yawed parent's.
    bend.offset = {0.5f, 0.0f, 0.0f};
    scene.get<Animator>(rig).adjust = {yaw, bend};
    check(
        "an offset moves the bone's origin along a model axis",
        at(origin(1), 0.5f, 1.0f, 0.0f) && at(origin(2), 0.5f, 1.0f, 1.0f)
    );

    // Absolute: the bone's final orientation, whatever the clip and parent did.
    // Identity on the spine under yawed hips stands it straight up; the head is back
    // over the origin.
    BoneAdjust stand;
    stand.bone     = 1;
    stand.absolute = true;
    scene.get<Animator>(rig).adjust = {yaw, stand};
    check("an absolute orientation ignores what the parent did", at(origin(2), 0.0f, 2.0f, 0.0f));

    // A halved spine halves what hangs off it: the head is half a unit up the spine.
    BoneAdjust shrink;
    shrink.bone  = 1;
    shrink.scale = 0.5f;
    scene.get<Animator>(rig).adjust = {shrink};
    check("a scale on a bone is inherited by what hangs off it", at(origin(2), 0.0f, 1.5f, 0.0f));

    // A bone the rig lacks adjusts nothing rather than the wrong one.
    BoneAdjust stray;
    stray.bone     = 9;
    stray.rotation = bend.rotation;
    scene.get<Animator>(rig).adjust = {stray};
    check("a bone index past the rig adjusts nothing", at(origin(2), 0.0f, 2.0f, 0.0f));
}

// A loadScene swaps the world mid-frame, after its ticks, rebuilding at the old slots.
// The pose buffer maps slots, so a frame taking no tick after the swap would hand the
// new rig the old rig's slice - fewer bones than its mesh indexes.
void testAReplacedWorldIsNotPosedByTheOldOne() {
    std::printf("A pose across a replaced world:\n");

    Scene     scene;
    TestFrame frame{scene};
    SkeletalAnimationSystem animation;

    const auto rigOf = [&](uint32_t bones, const char* name) {
        SkeletonAsset skeleton;
        for (uint32_t b = 0; b < bones; ++b) {
            skeleton.bones.push_back(Bone{"bone" + std::to_string(b), static_cast<int>(b) - 1});
        }
        skeleton.inverseBind.assign(bones, glm::mat4(1.0f));
        skeleton.bindPose.assign(bones, Transform{});
        return frame.resources.add(std::move(skeleton), name);
    };

    const EntityId oldRig = scene.createEntity();
    Animator small;
    small.skeleton = rigOf(1, "small");
    scene.add(oldRig, std::move(small));

    animation.fixedUpdate(frame.ctx);
    check(
        "the tick poses the old world's one-bone rig",
        frame.ctx.poses->sliceOf(oldRig) && frame.ctx.poses->sliceOf(oldRig)->count == 1
    );

    Scene replacement;
    const EntityId newRig = replacement.createEntity();
    Animator large;
    large.skeleton = rigOf(3, "large");
    replacement.add(newRig, std::move(large));
    scene.swap(replacement);

    // A running frame that took no tick: the clock is not paused.
    animation.update(frame.ctx);
    const PoseSlice* slice = frame.ctx.poses->sliceOf(newRig);
    check("the new world's rig sits at the old one's slot", newRig.slot() == oldRig.slot());
    check(
        "  and is posed in a slice of its own three bones, not the old one's one",
        slice && slice->count == 3
    );
}

// The pose map is written on the tick and read every frame until the next. A slot
// freed and reused in between belongs to an unposed entity; handing it the old slice
// would skin its mesh from another rig.
void testAReusedSlotIsNotPosedByTheEntityThatHadIt() {
    std::printf("A pose across a slot freed and taken again between ticks:\n");

    Scene     scene;
    TestFrame frame{scene};
    SkeletalAnimationSystem animation;

    SkeletonAsset skeleton;
    skeleton.bones.push_back(Bone{"root", -1});
    skeleton.inverseBind.assign(1, glm::mat4(1.0f));
    skeleton.bindPose.assign(1, Transform{});

    const EntityId rig = scene.createEntity();
    Animator animator;
    animator.skeleton = frame.resources.add(std::move(skeleton), "rig");
    scene.add(rig, std::move(animator));
    const EntityId part = scene.createEntity();
    HierarchyOperations::setParent(scene, part, rig);

    animation.fixedUpdate(frame.ctx);
    check(
        "the tick poses the rig and the entity under it",
        frame.ctx.poses->sliceOf(rig) && frame.ctx.poses->sliceOf(part)
    );

    scene.destroyEntity(part);
    const EntityId stranger = scene.createEntity();
    animation.update(frame.ctx);
    check("the stranger took the freed slot", stranger.slot() == part.slot());
    check("  and is not posed by the slice the slot had", !frame.ctx.poses->sliceOf(stranger));
    check("  while the rig still is", frame.ctx.poses->sliceOf(rig) != nullptr);
}

// A cooked skeleton or clip was judged by the reader; one built in code reaches the
// composer directly, which indexes by the invariants without checking them.
void testARigOrClipBuiltInCodeIsJudgedBeforeItIsPosed() {
    std::printf("A skeleton or clip built in code that breaks its invariants:\n");

    Scene     scene;
    TestFrame frame{scene};
    SkeletalAnimationSystem animation;

    // A bone naming itself as parent: the forward compose would read its own matrix
    // before writing it.
    SkeletonAsset loop;
    loop.bones = {Bone{"root", 0}};
    loop.inverseBind.assign(1, glm::mat4(1.0f));
    loop.bindPose.assign(1, Transform{});
    const EntityId broken = scene.createEntity();
    Animator brokenAnimator;
    brokenAnimator.skeleton = frame.resources.add(std::move(loop), "loop");
    scene.add(broken, std::move(brokenAnimator));

    // A well-formed rig playing a clip whose only fault is a marker past its end.
    SkeletonAsset whole;
    whole.bones = {Bone{"root", -1}};
    whole.inverseBind.assign(1, glm::mat4(1.0f));
    whole.bindPose.assign(1, Transform{});
    const SkeletonHandle wholeRig = frame.resources.add(std::move(whole), "whole");

    AnimationClipAsset lifted;
    lifted.skeleton      = "whole";
    lifted.duration      = 1.0f;
    lifted.bones.resize(1);
    lifted.bones[0].position = {0, 1};
    lifted.positionTimes = {0.0f};
    lifted.positions     = {glm::vec3(0.0f, 5.0f, 0.0f)};
    lifted.markers.push_back(ClipMarker{"late", 2.0f});
    const AnimationClipHandle liftedClip = frame.resources.add(std::move(lifted), "lifted");

    const EntityId rig = scene.createEntity();
    Animator animator;
    animator.skeleton = wholeRig;
    animator.clip     = liftedClip;
    scene.add(rig, std::move(animator));

    frame.clock.setPaused(true);
    animation.update(frame.ctx);    // paused composes the pose at a zero step

    check("a rig whose bone parents itself is left unposed", frame.ctx.poses->sliceOf(broken) == nullptr);

    const PoseSlice* slice = frame.ctx.poses->sliceOf(rig);
    check("  while a well-formed rig beside it is posed", slice != nullptr);
    check(
        "  holding its bind pose rather than playing a clip with a marker past its end",
        slice && nearly(frame.ctx.poses->global()[slice->first][3].y, 0.0f)
    );
}

} // namespace

void runAnimationTests() {
    testWhatPlayingAnAnimationDoes();
    testAPoseAtThePlayheadWritesOnlyKeyedChannels();
    testAClipRunBackwardsWrapsAndStopsLikeOneRunForwards();
    testATrackHoldsItsEndsRatherThanFallingOff();
    testATrackWithOneKeyIsThatKey();
    testKeysAreOrderedByTimeHoweverTheyArrive();
    testRetimingAKeyPastAnotherReportsWhereItLanded();
    testTwoKeysAtTheSameTimeDoNotDivideByZero();
    testEasingChangesTheShapeAndNotTheEnds();
    testEveryEasingSurvivesBeingWrittenDown();

    testALoopingClipAnnouncesEachMarkerOncePerLap();
    testANegativeSpeedComesBackOverTheSameMarkers();
    testANonLoopingClipStopsAtItsEnd();
    testAPausedClockAnnouncesNothing();
    testAScrubbedHeadAnnouncesNothingAndAStoppedOneNeither();
    testACrossfadeAnnouncesOnlyTheIncomingClip();
    testAOneShotFadingOutClampsRatherThanWrapping();
    testAFinishedOneShotAskedForAgainPlaysAgain();
    testAOneShotPlayedBackwardsStartsFromItsEnd();
    testTwoRigsOnOneClipAnnounceOnTheirOwnPhase();
    testRigsAnnounceInSlotOrder();

    testASocketRidesItsBone();
    testAnAdjustBendsABoneInModelSpace();
    testAReplacedWorldIsNotPosedByTheOldOne();
    testAReusedSlotIsNotPosedByTheEntityThatHadIt();
    testARigOrClipBuiltInCodeIsJudgedBeforeItIsPosed();
}
