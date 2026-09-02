#include "support.h"

#include "core/math/easing.h"
#include "system/animation/animation_track.h"

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

    // These pass with the zero-segment guard removed, and that is the finding:
    // upper_bound returns the first key strictly after the query, so the pair it
    // straddles can never share a time and segmentDuration is never zero from
    // here. The guard is defensive, not load-bearing, and what these pin is that
    // duplicate times produce a number rather than a NaN by any route.
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

} // namespace

void runAnimationTests() {
    testATrackHoldsItsEndsRatherThanFallingOff();
    testATrackWithOneKeyIsThatKey();
    testKeysAreOrderedByTimeHoweverTheyArrive();
    testTwoKeysAtTheSameTimeDoNotDivideByZero();
    testEasingChangesTheShapeAndNotTheEnds();
}
