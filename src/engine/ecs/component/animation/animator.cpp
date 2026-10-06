#include "ecs/component/animation/animator.h"

namespace Vkm::Engine {

void Animator::crossFadeTo(Animator& animator, AnimationClipHandle clip, float seconds, bool looping) {
    if (clip == animator.clip && animator.playing) return;

    if (!animator.clip || seconds <= 0.0f) {
        // A cut. A fade already in flight is cleared: it must not outlive the clip it blended into.
        animator.fadeFrom      = {};
        animator.fadeTime      = 0.0f;
        animator.fadeRemaining = 0.0f;
        animator.fadeDuration  = 0.0f;
    } else {
        animator.fadeFrom      = animator.clip;
        animator.fadeTime      = animator.time;
        animator.fadeRemaining = seconds;
        animator.fadeDuration  = seconds;
        animator.fadeLooping   = animator.looping;
    }

    animator.clip    = clip;
    animator.time    = 0.0f;
    animator.looping = looping;
    animator.playing = true;
}

} // namespace Vkm::Engine
