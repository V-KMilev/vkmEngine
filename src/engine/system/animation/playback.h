#pragma once

#include "ecs/component/animation/animator.h"

namespace Vkm::Engine {

/**
 * @brief The sweep a playback head made this frame.
 *
 * `to` is the head the Animator now carries, already wrapped or clamped, so it is exactly the next
 * frame's `from`. `travel` is signed and may exceed the clip's length (a hitch can step over whole
 * loops); it tells standing still from going all the way round.
 */
struct PlaybackStep {
    float from   = 0.0f;  ///< Head before the advance.
    float to     = 0.0f;  ///< Head after it, as the Animator now carries it.
    float travel = 0.0f;  ///< Signed seconds covered; 0 when nothing moved.
};

/**
 * @brief Move one playback head on and bring it back into its clip's range.
 *
 * Both ends are handled because `speed` may be negative. Wrapping is a floor-subtract, not fmod, because
 * fmod of a negative time stays negative.
 *
 * @param time Head to advance, in place.
 * @param duration Clip length in seconds; 0 disables wrapping.
 * @param delta Seconds to advance by, already scaled by the playback speed.
 * @param looping Whether the clip wraps rather than stopping at its end.
 * @return False when a non-looping clip has run out, the head clamped to the end it ran off.
 */
bool advanceHead(float& time, float duration, float delta, bool looping);

/**
 * @brief Put a one-shot head with nothing left to play in its direction back at the other end.
 *
 * What lets a clip start backwards (a head at 0 with negative speed is already at its end), and a
 * one-shot that ran out start over. For the playing head before it advances, never a fade's outgoing one.
 *
 * @param time Head to rewind, in place.
 * @param duration Clip length in seconds; 0 leaves the head alone.
 * @param delta The step about to be taken, already scaled by the playback speed.
 * @param looping Whether the clip wraps; a looping head is never spent.
 */
void rewindSpentHead(float& time, float duration, float delta, bool looping);

/**
 * @brief What a tick moves of an Animator: the playing head, whether it plays on, the fade's head and time
 *        left.
 */
struct PlaybackHeads {
    float time          = 0.0f;
    bool  playing       = false;
    float fadeTime      = 0.0f;
    float fadeRemaining = 0.0f;  ///< At or below 0 the fade is spent.

    /// @p animator's heads as they stand.
    static PlaybackHeads of(const Animator& animator) {
        return {animator.time, animator.playing, animator.fadeTime, animator.fadeRemaining};
    }
};

/**
 * @brief Move @p heads on by @p simDelta at @p animator's speed and looping: the one rule both a tick
 *        (advancePlayback) and a frame drawn between ticks (sampleAhead) move them by.
 *
 * @param animator Gives the speed and whether each clip loops.
 * @param heads Moved in place.
 * @param duration Length of the playing clip, seconds; 0 disables wrapping.
 * @param fromDuration Length of the clip being faded out of; 0 disables its wrapping.
 * @param simDelta Simulation seconds to advance by, above 0.
 * @return The sweep the playing head made.
 */
PlaybackStep advanceHeads(
    const Animator& animator,
    PlaybackHeads& heads,
    float duration,
    float fromDuration,
    float simDelta
);

/**
 * @brief Move @p animator's playback head(s) on by one frame and run down any fade in flight.
 *
 * Nothing moves while simulation time is stopped, so a time authored while paused survives. The
 * outgoing clip advances by the same delta, never stops the animator, and holds its last frame if it
 * runs out. The fade counts down in unscaled simulation seconds. `Animator::playOnStart` is honored
 * here, on the first frame with time to spend, so a rig starts on Play and holds in an open scene.
 *
 * @param animator Animator to advance, in place.
 * @param duration Length of the playing clip, seconds; 0 disables wrapping.
 * @param fromDuration Length of the clip being faded out of; 0 disables its wrapping.
 * @param simDelta Simulation seconds to advance by; 0 or less moves nothing.
 * @return The sweep the *playing* head made; stopped, paused or zero speed reports no travel, so no
 *         marker fires.
 */
PlaybackStep advancePlayback(Animator& animator, float duration, float fromDuration, float simDelta);

/**
 * @brief Whether @p step passed the instant @p marker names.
 *
 * Closed at the end arrived at, open at the end left, either direction: landing on a marker announces
 * it, moving off does not again. A step covering whole loops announces each marker once. A wrapped
 * sweep is one whose `to` lies behind its `from` in the direction of travel, which a clamped one never
 * does, so looping is not asked.
 *
 * @param step The sweep this frame's advance made.
 * @param marker Time the marker names, seconds into the clip.
 * @param duration Clip length in seconds; 0 makes every marker unreachable.
 * @return True when the marker should be announced this frame.
 */
bool crossesMarker(const PlaybackStep& step, float marker, float duration);

} // namespace Vkm::Engine
