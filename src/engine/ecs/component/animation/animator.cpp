#include "ecs/component/animation/animator.h"

namespace Vkm::Engine {

void Animator::crossFadeTo(Animator& animator, AnimationClipHandle clip, float seconds) {
    // Already playing it: a caller asking for the clip that is running means
    // "keep going", not "start it again from zero".
    if (clip == animator.clip) return;

    if (!animator.clip || seconds <= 0.0f) {
        // A cut. Nothing to blend out of, or no time to do it in - and clearing
        // is required either way, because a fade already in flight must not
        // outlive the clip it was blending into.
        animator.fadeFrom      = {};
        animator.fadeTime      = 0.0f;
        animator.fadeRemaining = 0.0f;
        animator.fadeDuration  = 0.0f;
    } else {
        // Two slots hold two clips. A fade already running loses the clip on its
        // way out, and blends from the one it was blending to - which is the one
        // the eye is actually on.
        animator.fadeFrom      = animator.clip;
        animator.fadeTime      = animator.time;
        animator.fadeRemaining = seconds;
        animator.fadeDuration  = seconds;
    }

    animator.clip    = clip;
    animator.time    = 0.0f;
    animator.playing = true;
}

} // namespace Vkm::Engine
