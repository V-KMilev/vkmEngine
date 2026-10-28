#include "ecs/component/animation/animator.h"

#include <utility>

namespace Vkm::Engine {

void Animator::crossFadeTo(Animator& animator, AnimationClipHandle clip, float seconds, bool looping) {
    if (clip == animator.clip && animator.playing) return;

    // Back to the clip on its way out: the fade turns round where it stands, the clip resuming at its
    // own head, so the pose does not jump.
    const bool fading = animator.fadeFrom && animator.fadeRemaining > 0.0f && animator.fadeDuration > 0.0f;
    if (fading && clip == animator.fadeFrom && seconds > 0.0f) {
        const float arrived = 1.0f - animator.fadeRemaining / animator.fadeDuration;
        animator.fadeFrom      = animator.clip;
        animator.clip          = clip;
        std::swap(animator.time, animator.fadeTime);
        animator.fadeLooping   = animator.looping;
        animator.looping       = looping;
        animator.fadeDuration  = seconds;
        animator.fadeRemaining = seconds * arrived;
        animator.playing       = true;
        // Turned round before it moved: nothing of the other clip shows, so nothing is left to fade.
        if (animator.fadeRemaining <= 0.0f) {
            animator.fadeFrom     = {};
            animator.fadeTime     = 0.0f;
            animator.fadeDuration = 0.0f;
        }
        return;
    }

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
