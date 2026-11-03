#include "system/animation/playback.h"

#include <algorithm>
#include <cmath>

namespace Vkm::Engine {

bool advanceHead(float& time, float duration, float delta, bool looping) {
    time += delta;
    if (duration <= 0.0f) return true;

    if (looping) {
        time -= std::floor(time / duration) * duration;
    } else if (time >= duration) {
        time = duration;
        return false;
    } else if (time < 0.0f) {
        time = 0.0f;
        return false;
    }
    return true;
}

void rewindSpentHead(float& time, float duration, float delta, bool looping) {
    if (looping || duration <= 0.0f) return;
    if (delta < 0.0f && time <= 0.0f) {
        time = duration;
    } else if (delta > 0.0f && time >= duration) {
        time = 0.0f;
    }
}

PlaybackStep advanceHeads(
    const Animator& animator,
    PlaybackHeads& heads,
    float duration,
    float fromDuration,
    float simDelta
) {
    // Rewound before the sweep is measured, so it starts where playing starts.
    const float delta = simDelta * animator.speed;
    if (heads.playing) rewindSpentHead(heads.time, duration, delta, animator.looping);

    PlaybackStep step{heads.time, heads.time, 0.0f};
    if (heads.playing) {
        if (!advanceHead(heads.time, duration, delta, animator.looping)) heads.playing = false;
        step.to = heads.time;
        // Subtracting misreads a wrapped head: a loop covers what was asked, a clamp what it allowed.
        step.travel = animator.looping ? delta : heads.time - step.from;
    }

    // Not gated on `playing`: a one-shot ending mid-blend would strand the fade at a weight no field names.
    if (heads.fadeRemaining > 0.0f) {
        advanceHead(heads.fadeTime, fromDuration, delta, animator.fadeLooping);
        heads.fadeRemaining -= simDelta;
    }
    return step;
}

PlaybackStep advancePlayback(Animator& animator, float duration, float fromDuration, float simDelta) {
    if (simDelta <= 0.0f) return PlaybackStep{animator.time, animator.time, 0.0f};

    if (animator.playOnStart && !animator.started) {
        animator.started = true;
        animator.playing = true;
    }

    const bool fading = animator.fadeRemaining > 0.0f;
    PlaybackHeads heads = PlaybackHeads::of(animator);
    const PlaybackStep step = advanceHeads(animator, heads, duration, fromDuration, simDelta);

    animator.time          = heads.time;
    animator.playing       = heads.playing;
    animator.fadeTime      = heads.fadeTime;
    animator.fadeRemaining = heads.fadeRemaining;
    if (!fading || animator.fadeRemaining > 0.0f) return step;

    animator.fadeFrom      = {};
    animator.fadeTime      = 0.0f;
    animator.fadeRemaining = 0.0f;
    animator.fadeDuration  = 0.0f;
    return step;
}

bool crossesMarker(const PlaybackStep& step, float marker, float duration) {
    if (step.travel == 0.0f || duration <= 0.0f) return false;

    if (std::abs(step.travel) >= duration) return true;

    // Otherwise the wrap branches read a sub-resolution move as a full lap.
    if (step.from == step.to) return false;

    if (step.travel > 0.0f) {
        return (step.from < step.to)
            ? (marker > step.from && marker <= step.to)   // straight through
            : (marker > step.from || marker <= step.to);  // wrapped past the end
    }
    return (step.to < step.from)
        ? (marker >= step.to && marker < step.from)
        : (marker >= step.to || marker < step.from);      // wrapped past the start
}

} // namespace Vkm::Engine
