#include "support.h"

#include <algorithm>

#include "core/math/easing.h"
#include "ecs/component/animation/animation_track.h"
#include "ecs/component/animation/animator.h"
#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "system/animation/animation_events.h"
#include "system/animation/skeletal_animation_system.h"

namespace {

// The roadmap names "the animation last-frame drop" as one of the defects a
// test pins forever, and the track had none. Every boundary getValue special-
// cases is asserted here, because each one returns a plausible number when it
// is wrong: a pose, just not the authored one.
void testATrackHoldsItsEndsRatherThanFallingOff() {
    std::printf("What a track answers outside the keys it has:\n");

    AnimationTrack<float> track;
    track.addKeyframe(1.0f, 10.0f);
    track.addKeyframe(3.0f, 30.0f);

    check("the last key is reachable", nearly(track.getValue(3.0f), 30.0f));
    check("  and holds past the end", nearly(track.getValue(99.0f), 30.0f));
    check("the first key holds before the start", nearly(track.getValue(0.0f), 10.0f));

    // The first key is at 1.0, not 0. Asking at 0.5 lands before every key, and
    // an unguarded upper_bound would hand back begin() and underflow the
    // previous index to SIZE_MAX - the block above getValue says so.
    check("  including between zero and a first key that is not at zero",
          nearly(track.getValue(0.5f), 10.0f));
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

    // getValue binary-searches the times, so an unsorted track does not fail -
    // it answers the wrong key, quietly.
    check("three keys are held", track.keyframeCount() == 3);
    check("the earliest is first", nearly(track.getValue(1.0f), 10.0f));
    check("  the middle is between them", nearly(track.getValue(2.0f), 20.0f));
    check("  and the latest is last", nearly(track.getValue(3.0f), 30.0f));
    check("interpolation runs the right way round", nearly(track.getValue(1.5f), 15.0f));
}

void testTwoKeysAtTheSameTimeDoNotDivideByZero() {
    std::printf("Two keys sharing one time:\n");

    AnimationTrack<float> track;
    track.addKeyframe(1.0f, 10.0f);
    track.addKeyframe(1.0f, 20.0f);
    track.addKeyframe(2.0f, 30.0f);

    // upper_bound returns the first key strictly after the query, so the pair it
    // straddles never shares a time and the zero-segment guard is defensive rather
    // than load-bearing. What these pin is a number rather than a NaN, by any route.
    const float at = track.getValue(1.0f);
    check("the answer at the shared time is a number", std::isfinite(at));
    check("  and so is one just after it", std::isfinite(track.getValue(1.0001f)));
    check("  and one between it and the next key", std::isfinite(track.getValue(1.5f)));
}

void testEasingChangesTheShapeAndNotTheEnds() {
    std::printf("A track with a curve on it:\n");

    AnimationTrack<float> track;
    track.addKeyframe(0.0f, 0.0f);
    track.addKeyframe(1.0f, 100.0f);
    track.setEasing(Easing::byName("easeInOutSine"));

    // Whatever the curve, the keys themselves are what the author placed.
    check("the start key is untouched by easing", nearly(track.getValue(0.0f), 0.0f));
    check("  and the end key is too", nearly(track.getValue(1.0f), 100.0f));

    const float mid = track.getValue(0.5f);
    check("the middle is still inside the range", mid > 0.0f && mid < 100.0f);

    AnimationTrack<float> linear;
    linear.addKeyframe(0.0f, 0.0f);
    linear.addKeyframe(1.0f, 100.0f);
    check("and an eased quarter differs from a linear one",
          !nearly(track.getValue(0.25f), linear.getValue(0.25f)));
}

// The marker path, driven through the real SkeletalAnimationSystem over a real
// Scene. Every claim about a marker is a claim about how many events landed on
// the bus over a run of frames, so counting them off the system is the only
// thing that can settle one - re-deriving crossesMarker's arithmetic here would
// pass whatever that arithmetic became.
//
// The clock is single-stepped through requestStep(), the editor's own path, so
// each frame is exactly one fixed step and the expected counts are arithmetic
// rather than a tolerance.
struct MarkerWorld {
    Scene           scene;
    TestFrame       frame{scene};
    SkeletalAnimationSystem animation;
    EntityId        rig;
    std::vector<std::string> heard;   ///< Marker names, in the order they arrived.
    std::vector<size_t>      perFrame;///< How many arrived on each frame.

    MarkerWorld(float duration, std::vector<ClipMarker> markers, bool looping = true,
                float speed = 1.0f, int tickRate = 60) {
        // One bone is a rig: the marker path never looks at the pose, and a
        // second bone would only make the fixture longer.
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

    /// Run @p frames single-step frames, each exactly one fixed step.
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

    // One second long at 60 Hz, so a lap is exactly 60 frames and 600 frames
    // are exactly ten laps. Markers off the frame boundaries, so none of them
    // lands on the seam where a lap wraps.
    MarkerWorld world(1.0f, {{"a", 0.25f}, {"b", 0.5f}, {"c", 0.75f}});
    world.tick(600);

    check("each marker announced once per lap, ten laps in", world.countOf("a") == 10
          && world.countOf("b") == 10 && world.countOf("c") == 10);
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
    check("and no frame announced more than the one marker it crossed",
          oneFramePerMarker);
}

void testANegativeSpeedComesBackOverTheSameMarkers() {
    std::printf("A clip played backwards:\n");

    MarkerWorld world(1.0f, {{"a", 0.25f}, {"b", 0.5f}, {"c", 0.75f}},
                      /*looping*/ true, /*speed*/ -1.0f);
    world.tick(600);

    check("each marker still announced once per lap", world.countOf("a") == 10
          && world.countOf("b") == 10 && world.countOf("c") == 10);
    check("and they arrived in the reverse order",
          world.heard.size() >= 3 && world.heard[0] == "c"
          && world.heard[1] == "b" && world.heard[2] == "a");
}

void testANonLoopingClipStopsAtItsEnd() {
    std::printf("A clip that does not loop:\n");

    // The marker sits on the last instant the head can reach, and which frame that
    // is depends on float accumulation over sixty steps - so this runs until the
    // head clamps rather than naming a frame.
    MarkerWorld world(1.0f, {{"end", 1.0f}}, /*looping*/ false);
    int frames = 0;
    while (world.animator().playing && frames < 300) {
        world.tick(1);
        ++frames;
    }
    check("the head reached the end and stopped", !world.animator().playing);
    check("and the marker is announced on the frame it landed there",
          world.countOf("end") == 1 && !world.perFrame.empty()
          && world.perFrame.back() == 1);

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
    check("the frames that resume announce the marker one lap reaches",
          world.countOf("a") == 1);
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
    check("and a stopped animator announces nothing over three hundred frames",
          stopped.heard.empty());
}

void testACrossfadeAnnouncesOnlyTheIncomingClip() {
    std::printf("A crossfade between two marked clips:\n");

    MarkerWorld world(1.0f, {{"incoming", 0.5f}});

    // A second clip on the same rig, marked at a time the outgoing head will
    // cross while the fade is still in flight.
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
    // Fade into the marked clip over half a second: the outgoing head reaches
    // its own marker six frames in, well inside the fade.
    Animator::crossFadeTo(animator, world.frame.resources.findByName<AnimationClipAsset>("clip"),
                          0.5f, /*looping*/ true);
    check("the fade is in flight", world.animator().fadeRemaining > 0.0f);

    world.tick(12);
    check("the outgoing clip's marker is not announced", world.countOf("outgoing") == 0);
    check("and the fade was still running while it would have been",
          world.animator().fadeRemaining > 0.0f);

    world.tick(48);
    check("the incoming clip's marker is announced", world.countOf("incoming") == 1);
}

void testAOneShotFadingOutClampsRatherThanWrapping() {
    std::printf("A one-shot fading out under a looping clip:\n");

    // The outgoing clip is a fifth of a second and does not loop; the fade is
    // half a second, so the head reaches the end of the clip with the blend
    // still running. It must stop there, not start again for the rest of it.
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
    Animator::crossFadeTo(animator, world.frame.resources.findByName<AnimationClipAsset>("clip"),
                          0.5f, /*looping*/ true);
    check("the incoming clip loops", world.animator().looping);
    check("and the outgoing one is remembered as not looping",
          !world.animator().fadeLooping);

    world.tick(24);   // 0.4s: twice the outgoing clip's length
    check("the fade is still running", world.animator().fadeRemaining > 0.0f);
    check("and the outgoing head stopped at the end of its clip rather than wrapping",
          nearly(world.animator().fadeTime, 0.2f));
}

void testTwoRigsOnOneClipAnnounceOnTheirOwnPhase() {
    std::printf("Two characters half a lap apart on one clip:\n");

    MarkerWorld world(1.0f, {{"a", 0.25f}});

    const EntityId second = world.scene.createEntity();
    Animator other = world.animator();   // same handles, its own head
    other.time = 0.5f;
    world.scene.add(second, std::move(other));

    std::vector<EntityId> from;
    world.frame.events.subscribe<AnimationEvent>(
        [&](const AnimationEvent& e) { from.push_back(e.entity); });

    world.tick(60);
    check("each rig announced once over the lap", world.countOf("a") == 2);
    check("and they were two different rigs",
          from.size() == 2 && from[0] != from[1]);
}

} // namespace

void runAnimationTests() {
    testATrackHoldsItsEndsRatherThanFallingOff();
    testATrackWithOneKeyIsThatKey();
    testKeysAreOrderedByTimeHoweverTheyArrive();
    testTwoKeysAtTheSameTimeDoNotDivideByZero();
    testEasingChangesTheShapeAndNotTheEnds();

    testALoopingClipAnnouncesEachMarkerOncePerLap();
    testANegativeSpeedComesBackOverTheSameMarkers();
    testANonLoopingClipStopsAtItsEnd();
    testAPausedClockAnnouncesNothing();
    testAScrubbedHeadAnnouncesNothingAndAStoppedOneNeither();
    testACrossfadeAnnouncesOnlyTheIncomingClip();
    testAOneShotFadingOutClampsRatherThanWrapping();
    testTwoRigsOnOneClipAnnounceOnTheirOwnPhase();
}
